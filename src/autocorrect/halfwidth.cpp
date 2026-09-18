/* rule/halfwidth.rs — the two AFTER_RULES that pull English text back to
   halfwidth: fullwidth alphanumerics become ASCII (halfwidth-word), and a
   line that reads as English-only gets its fullwidth punctuation swapped
   for ASCII (halfwidth-punctuation).

   Part of the C++ port of the `autocorrect` crate 2.14.2 (see
   src/autocorrect/readme.md). */

#include "autocorrect/internal.h"

namespace autocorrect {

static bool IsAsciiDigitCp(uint32_t cp) {
    return cp >= '0' && cp <= '9';
}

// char::is_alphanumeric — Unicode letters and digits, without '_'.
static bool IsAlnumCharCp(uint32_t cp) {
    return cp != '_' && IsWordCp(cp);
}

static bool IsWhitespaceCp(uint32_t cp) {
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\f' ||
        cp == 0x0B || cp == 0x85 || cp == 0xA0) {
        return true;
    }
    return cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 ||
           cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

static void AppendCp(StrBuilder* out, uint32_t cp) {
    char buf[4];
    int n;
    if (cp < 0x80) {
        buf[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        n = 4;
    }
    out->Append(Str(buf, n));
}

// ─── halfwidth-word (format_word) ─────────────────────────────────────────

bool FormatHalfwidthWord(Arena* a, Str in, Str* out) {
    StrBuilder b;
    bool changed = false;
    for (int i = 0; i < len(in);) {
        uint32_t cp = Utf8Next(in, &i);
        // Fullwidth ０-９ Ａ-Ｚ ａ-ｚ → ASCII; ideographic space → space.
        if ((cp >= 0xFF10 && cp <= 0xFF19) || (cp >= 0xFF21 && cp <= 0xFF3A) ||
            (cp >= 0xFF41 && cp <= 0xFF5A)) {
            AppendCp(&b, cp - 0xFEE0);
            changed = true;
            continue;
        }
        if (cp == 0x3000) {
            b.AppendChar(' ');
            changed = true;
            continue;
        }
        AppendCp(&b, cp);
    }
    // HALF_TIME_RE `(\d)(：)(\d)`: a fullwidth colon between digits becomes
    // ':' — 16：32 → 16:32.
    Str cur = changed ? Str(b.els, b.len) : in;
    StrBuilder t;
    bool timeHit = false;
    for (int i = 0; i < len(cur);) {
        int save = i;
        uint32_t cp = Utf8Next(cur, &i);
        if (IsAsciiDigitCp(cp) && i < len(cur)) {
            int j = i;
            uint32_t c2 = Utf8Next(cur, &j);
            if (c2 == 0xFF1A && j < len(cur) && // ：
                IsAsciiDigitCp(Utf8At(cur, j))) {
                AppendCp(&t, cp);
                t.AppendChar(':');
                uint32_t c3 = Utf8Next(cur, &j);
                AppendCp(&t, c3);
                i = j;
                timeHit = true;
                continue;
            }
        }
        t.Append(Str(cur.s + save, i - save));
    }
    if (!changed && !timeHit) {
        return false;
    }
    *out = base::StrDup(a, timeHit ? Str(t.els, t.len) : cur);
    return true;
}

// ─── halfwidth-punctuation (format_punctuation) ───────────────────────────

// PUNCTUATION_MAP.
enum class ReplaceMode : uint8_t {
    Replace,
    PrefixSpace,
    SuffixSpace
};
enum class CharType : uint8_t {
    LeftQuote,
    RightQuote,
    Other
};

struct ReplaceRule {
    uint32_t from;
    uint32_t to;
    ReplaceMode mode;
    CharType type;
};

static const ReplaceRule kPunctuationMap[] = {
    {0xFF0C, ',', ReplaceMode::SuffixSpace, CharType::Other},         // ，
    {0x3001, ',', ReplaceMode::SuffixSpace, CharType::Other},         // 、
    {0x3002, '.', ReplaceMode::SuffixSpace, CharType::Other},         // 。
    {0xFF1A, ':', ReplaceMode::SuffixSpace, CharType::Other},         // ：
    {0xFF1B, '.', ReplaceMode::SuffixSpace, CharType::Other},         // ；
    {0xFF01, '!', ReplaceMode::SuffixSpace, CharType::Other},         // ！
    {0xFF1F, '?', ReplaceMode::SuffixSpace, CharType::Other},         // ？
    {0xFF08, '(', ReplaceMode::PrefixSpace, CharType::LeftQuote},     // （
    {0x3010, '[', ReplaceMode::PrefixSpace, CharType::LeftQuote},     // 【
    {0x300C, '[', ReplaceMode::PrefixSpace, CharType::LeftQuote},     // 「
    {0x300A, 0x201C, ReplaceMode::PrefixSpace, CharType::LeftQuote},  // 《→“
    {0xFF09, ')', ReplaceMode::SuffixSpace, CharType::RightQuote},    // ）
    {0x3011, ']', ReplaceMode::SuffixSpace, CharType::RightQuote},    // 】
    {0x300D, ']', ReplaceMode::SuffixSpace, CharType::RightQuote},    // 」
    {0x300B, 0x201D, ReplaceMode::SuffixSpace, CharType::RightQuote}, // 》→”
};

static const ReplaceRule* RuleFor(uint32_t cp) {
    for (const ReplaceRule& r : kPunctuationMap) {
        if (r.from == cp) {
            return &r;
        }
    }
    return nullptr;
}

// ENGLISH_RE `([\w]+[ ,.'?!&:]+[\w]+)`: a word, separators, a word.
static bool IsEnglishSep(uint32_t cp) {
    return cp == ' ' || cp == ',' || cp == '.' || cp == '\'' || cp == '?' ||
           cp == '!' || cp == '&' || cp == ':';
}

static bool HasEnglishShape(Str s) {
    int i = 0;
    while (i < len(s)) {
        if (!IsWordCp(Utf8At(s, i))) {
            Utf8Next(s, &i);
            continue;
        }
        // A word run…
        while (i < len(s) && IsWordCp(Utf8At(s, i))) {
            Utf8Next(s, &i);
        }
        // …then at least one separator…
        int seps = 0;
        while (i < len(s) && IsEnglishSep(Utf8At(s, i))) {
            Utf8Next(s, &i);
            seps++;
        }
        // …then a word char.
        if (seps > 0 && i < len(s) && IsWordCp(Utf8At(s, i))) {
            return true;
        }
    }
    return false;
}

// START_WITH_WORD_RE `^\s*[\w]+`.
static bool StartsWithWord(Str s) {
    int i = 0;
    while (i < len(s) && IsWhitespaceCp(Utf8At(s, i))) {
        Utf8Next(s, &i);
    }
    return i < len(s) && IsWordCp(Utf8At(s, i));
}

// QUOTE_RE `^\s*(["'`]).+(["'`])\s*$`.
static bool IsQuoteCh(uint32_t cp) {
    return cp == '"' || cp == '\'' || cp == '`';
}

static bool IsQuoted(Str s) {
    int first = 0;
    while (first < len(s) && IsWhitespaceCp(Utf8At(s, first))) {
        Utf8Next(s, &first);
    }
    if (first >= len(s) || !IsQuoteCh(Utf8At(s, first))) {
        return false;
    }
    int last = -1;
    for (int i = first; i < len(s);) {
        int at = i;
        uint32_t cp = Utf8Next(s, &i);
        if (IsQuoteCh(cp)) {
            // A closing candidate only if just whitespace follows.
            int j = i;
            bool tail = true;
            while (j < len(s)) {
                if (!IsWhitespaceCp(Utf8At(s, j))) {
                    tail = false;
                    break;
                }
                Utf8Next(s, &j);
            }
            if (tail) {
                last = at;
            }
        }
    }
    if (last <= first) {
        return false;
    }
    // `.+` between the quotes: at least one char, no newline.
    if (last == first + 1) {
        return false;
    }
    for (int i = first + 1; i < last; i++) {
        if (s.s[i] == '\n') {
            return false;
        }
    }
    return true;
}

// WORD_RE `[a-zA-Z]{2,}`.
static bool HasTwoLetters(Str s) {
    int run = 0;
    for (int i = 0; i < len(s); i++) {
        char c = s.s[i];
        bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        run = letter ? run + 1 : 0;
        if (run >= 2) {
            return true;
        }
    }
    return false;
}

// CODE_STRING_RE `([#%$]\{.+\})|([\w]+\.[\w]+\()`.
static bool LooksLikeCodeString(Str s) {
    for (int i = 0; i < len(s); i++) {
        char c = s.s[i];
        if ((c == '#' || c == '%' || c == '$') && i + 2 < len(s) &&
            s.s[i + 1] == '{') {
            for (int j = i + 3; j < len(s) && s.s[j] != '\n'; j++) {
                if (s.s[j] == '}') {
                    return true;
                }
            }
        }
    }
    int i = 0;
    while (i < len(s)) {
        if (!IsWordCp(Utf8At(s, i))) {
            Utf8Next(s, &i);
            continue;
        }
        while (i < len(s) && IsWordCp(Utf8At(s, i))) {
            Utf8Next(s, &i);
        }
        if (i + 1 < len(s) && s.s[i] == '.' && IsWordCp(Utf8At(s, i + 1))) {
            int j = i + 1;
            while (j < len(s) && IsWordCp(Utf8At(s, j))) {
                Utf8Next(s, &j);
            }
            if (j < len(s) && s.s[j] == '(') {
                return true;
            }
        }
    }
    return false;
}

static bool IsMayOnlyEnglish(Str text) {
    if (HasCjk(text)) {
        return false;
    }
    if (HasEnglishShape(text) && StartsWithWord(text)) {
        return true;
    }
    if (IsQuoted(text) && HasTwoLetters(text)) {
        // `${this.$t('hello')}：${items.join('，')}` and the like keep their
        // fullwidth punctuation: it is data, not prose.
        if (LooksLikeCodeString(text)) {
            return false;
        }
        return true;
    }
    return false;
}

static void EscapeQuote(StrBuilder* out, uint32_t wrapQuote, uint32_t quote,
                        uint32_t* lastCp) {
    if ((quote == '"' || quote == '\'') && wrapQuote == quote) {
        out->AppendChar('\\');
    }
    AppendCp(out, quote);
    *lastCp = quote;
}

static bool FormatLine(Str line, uint32_t wrapQuote, StrBuilder* out) {
    if (!IsMayOnlyEnglish(line)) {
        out->Append(line);
        return false;
    }
    bool changed = false;
    uint32_t lastCp = 0;
    bool hasLast = false;
    for (int i = 0; i < len(line);) {
        uint32_t cp = Utf8Next(line, &i);
        const ReplaceRule* rule = RuleFor(cp);
        if (!rule) {
            AppendCp(out, cp);
            lastCp = cp;
            hasLast = true;
            continue;
        }
        bool hasNext = i < len(line);
        uint32_t next = hasNext ? Utf8At(line, i) : 0;
        // A left quote as the very last char stays: "Escher puzzle（".
        if (!hasNext && rule->type == CharType::LeftQuote) {
            AppendCp(out, cp);
            lastCp = cp;
            hasLast = true;
            continue;
        }
        switch (rule->mode) {
            case ReplaceMode::SuffixSpace:
                EscapeQuote(out, wrapQuote, rule->to, &lastCp);
                hasLast = true;
                if (hasNext && IsAlnumCharCp(next)) {
                    out->AppendChar(' ');
                    lastCp = ' ';
                }
                break;
            case ReplaceMode::PrefixSpace:
                if (hasLast && IsAlnumCharCp(lastCp)) {
                    out->AppendChar(' ');
                }
                EscapeQuote(out, wrapQuote, rule->to, &lastCp);
                hasLast = true;
                break;
            case ReplaceMode::Replace:
            default:
                EscapeQuote(out, wrapQuote, rule->to, &lastCp);
                hasLast = true;
                break;
        }
        changed = true;
    }
    return changed;
}

bool FormatHalfwidthPunctuation(Arena* a, Str in, Str* out) {
    // The first non-whitespace char is the quote the whole text is wrapped
    // in, if any — what EscapeQuote escapes against.
    uint32_t wrapQuote = ' ';
    for (int i = 0; i < len(in);) {
        uint32_t cp = Utf8Next(in, &i);
        if (!IsWhitespaceCp(cp)) {
            wrapQuote = cp;
            break;
        }
    }
    StrBuilder b;
    bool changed = false;
    int lineStart = 0;
    for (int i = 0; i <= len(in); i++) {
        bool eol = i == len(in) || in.s[i] == '\n';
        if (!eol) {
            continue;
        }
        // split_inclusive('\n'): the line keeps its newline.
        int end = i == len(in) ? i : i + 1;
        if (end > lineStart) {
            changed |= FormatLine(Str(in.s + lineStart, end - lineStart),
                                  wrapQuote, &b);
        }
        lineStart = end;
    }
    if (!changed) {
        return false;
    }
    *out = base::StrDup(a, Str(b.els, b.len));
    return true;
}

} // namespace autocorrect
