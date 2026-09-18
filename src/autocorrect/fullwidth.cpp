/* rule/fullwidth.rs — halfwidth punctuation near CJK becomes fullwidth:
   "你好,这是一个句子." → "你好，这是一个句子。".

   Part of the C++ port of the `autocorrect` crate 2.14.2 (see
   src/autocorrect/readme.md).

   The crate runs four regexes over the text in order, each replaced through
   fullwidth_replace_part, which swaps every `[.:!]` / `[,?]` (with its
   trailing spaces) in the matched span for the fullwidth char. Its `\p{CJ}`
   inside a character class leaves a literal '|' in the class, kept here. */

#include "autocorrect/internal.h"

namespace autocorrect {

// `[\p{CJ}|]` — Han / Katakana / Hiragana / Bopomofo, plus the macro's '|'.
static bool IsCjClassCp(uint32_t cp) {
    return cp == '|' || IsCj(cp);
}

// `[\p{CJ}|\w\d]` — the left side of PUNCTUATION_WITH_LEFT_CJK_RE.
static bool IsCjOrWordClassCp(uint32_t cp) {
    return IsCjClassCp(cp) || IsWordCp(cp);
}

static bool IsNormalPunct(char c) {
    return c == ',' || c == '?';
}

static bool IsSpecialPunct(char c) {
    return c == '.' || c == ':' || c == '!';
}

// A run of class chars starting at i; byte length (0 when none).
static int MatchClassRun(Str s, int i, bool (*pred)(uint32_t)) {
    int at = i;
    while (at < len(s)) {
        int next = at;
        if (!pred(Utf8Next(s, &next))) {
            break;
        }
        at = next;
    }
    return at - i;
}

// FULLWIDTH_MAPS, minus the ';' entry no pattern ever feeds it.
static Str FullwidthFor(char c) {
    switch (c) {
        case ',':
            return StrL("，"); // ，
        case '.':
            return StrL("。"); // 。
        case ':':
            return StrL("："); // ：
        case '!':
            return StrL("！"); // ！
        case '?':
            return StrL("？"); // ？
        default:
            return {};
    }
}

// fullwidth_replace_part: every `[.:!][ ]*` / `[,?][ ]*` in the span becomes
// the fullwidth char, trailing spaces dropped.
static void AppendMappedSpan(StrBuilder* out, Str span) {
    int i = 0;
    while (i < len(span)) {
        char c = span.s[i];
        if (IsNormalPunct(c) || IsSpecialPunct(c)) {
            out->Append(FullwidthFor(c));
            i++;
            while (i < len(span) && span.s[i] == ' ') {
                i++;
            }
            continue;
        }
        int step = Utf8Len(span, i);
        out->Append(Str(span.s + i, step));
        i += step;
    }
}

// The four patterns. Each Match* answers the byte length matched at i or -1.

// `[\p{CJ}|\w\d]+ [,?] [ ]* [\p{CJ}|]+`
static int MatchLeft(Str s, int i) {
    int a = MatchClassRun(s, i, IsCjOrWordClassCp);
    if (a <= 0) {
        return -1;
    }
    int at = i + a;
    if (at >= len(s) || !IsNormalPunct(s.s[at])) {
        return -1;
    }
    at++;
    while (at < len(s) && s.s[at] == ' ') {
        at++;
    }
    int b = MatchClassRun(s, at, IsCjClassCp);
    if (b <= 0) {
        return -1;
    }
    return at + b - i;
}

// `[\p{CJ}|]+ [,?] [ ]*`
static int MatchRight(Str s, int i) {
    int a = MatchClassRun(s, i, IsCjClassCp);
    if (a <= 0) {
        return -1;
    }
    int at = i + a;
    if (at >= len(s) || !IsNormalPunct(s.s[at])) {
        return -1;
    }
    at++;
    while (at < len(s) && s.s[at] == ' ') {
        at++;
    }
    return at - i;
}

// `[\p{CJ}|]+ [.:!] [ ]* [\p{CJ}|]+`
static int MatchSpecial(Str s, int i) {
    int a = MatchClassRun(s, i, IsCjClassCp);
    if (a <= 0) {
        return -1;
    }
    int at = i + a;
    if (at >= len(s) || !IsSpecialPunct(s.s[at])) {
        return -1;
    }
    at++;
    while (at < len(s) && s.s[at] == ' ') {
        at++;
    }
    int b = MatchClassRun(s, at, IsCjClassCp);
    if (b <= 0) {
        return -1;
    }
    return at + b - i;
}

// `[\p{CJ}|]+ [.:!] [ ]* ["']? $`
static int MatchSpecialLast(Str s, int i) {
    int a = MatchClassRun(s, i, IsCjClassCp);
    if (a <= 0) {
        return -1;
    }
    int at = i + a;
    if (at >= len(s) || !IsSpecialPunct(s.s[at])) {
        return -1;
    }
    at++;
    while (at < len(s) && s.s[at] == ' ') {
        at++;
    }
    if (at < len(s) && (s.s[at] == '"' || s.s[at] == '\'')) {
        at++;
    }
    return at == len(s) ? at - i : -1;
}

using MatchFn = int (*)(Str, int);

static bool PassReplace(Str in, MatchFn match, StrBuilder* out) {
    bool changed = false;
    int i = 0;
    while (i < len(in)) {
        int n = match(in, i);
        if (n > 0) {
            AppendMappedSpan(out, Str(in.s + i, n));
            i += n;
            changed = true;
            continue;
        }
        int step = Utf8Len(in, i);
        out->Append(Str(in.s + i, step));
        i += step;
    }
    return changed;
}

bool FormatFullwidth(Arena* a, Str in, Str* out) {
    static const MatchFn kPatterns[] = {MatchLeft, MatchRight, MatchSpecial,
                                        MatchSpecialLast};
    Str cur = in;
    bool changed = false;
    StrBuilder b;
    for (MatchFn pattern : kPatterns) {
        b.Reset();
        if (PassReplace(cur, pattern, &b)) {
            cur = base::StrDup(a, Str(b.els, b.len));
            changed = true;
        }
    }
    if (changed) {
        *out = cur;
    }
    return changed;
}

} // namespace autocorrect
