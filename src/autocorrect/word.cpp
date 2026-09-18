/* rule/word.rs + rule/strategery.rs — the space-adding and space-removing
   rules, each a pair of character-class patterns the crate builds a regex
   from. Here each pattern side is a matcher function and Strategery's
   replace_all is the driver loop, resuming after a match the way
   Regex::replace_all does.

   Part of the C++ port of the `autocorrect` crate 2.14.2 (see
   src/autocorrect/readme.md).

   One transcription note: the crate's `\p{CJK}` macro expands to the
   alternation `\p{Han}|\p{Hangul}|\p{Katakana}|\p{Hiragana}|\p{Bopomofo}`
   with no group around it, so in `\p{CJK}[^%\$\\]` the suffix binds to the
   Bopomofo alternative only — SideCjkWordOne below keeps that shape rather
   than the shape the comment in word.rs describes. Inside a character class
   `\p{CJK_N}` expands with a literal `|`, so those classes match '|' too. */

#include "autocorrect/internal.h"

namespace autocorrect {

// ─── matcher helpers ──────────────────────────────────────────────────────

// A side matcher: byte length matched at s[i], or -1.
using SideFn = int (*)(Str s, int i);

static bool IsAsciiAlnumCp(uint32_t cp) {
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
           (cp >= '0' && cp <= '9');
}

// The regex crate's \s (Unicode White_Space), the parts of it that occur in
// documents this linter sees.
static bool IsSpaceCp(uint32_t cp) {
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\f' ||
        cp == 0x0B || cp == 0x85 || cp == 0xA0) {
        return true;
    }
    return cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 ||
           cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

// `[\p{CJK_N}…]` classes: the five scripts plus the literal '|' the macro
// leaves in the class, plus whatever extra chars the class names.
static bool IsCjkClassCp(uint32_t cp) {
    return cp == '|' || IsCjk(cp);
}

static int MatchCp(Str s, int i, bool (*pred)(uint32_t)) {
    if (i >= len(s)) {
        return -1;
    }
    int at = i;
    uint32_t cp = Utf8Next(s, &at);
    return pred(cp) ? at - i : -1;
}

// ─── the pattern sides ────────────────────────────────────────────────────

// `\p{Han}|\p{Hangul}|\p{Katakana}|\p{Hiragana}|\p{Bopomofo}[^%\$\\]`
static int SideCjkWordOne(Str s, int i) {
    if (i >= len(s)) {
        return -1;
    }
    int at = i;
    uint32_t cp = Utf8Next(s, &at);
    if (IsHan(cp) || IsHangul(cp) || IsKatakana(cp) || IsHiragana(cp)) {
        return at - i;
    }
    if (IsBopomofo(cp) && at < len(s)) {
        uint32_t c2 = Utf8Next(s, &at);
        if (c2 != '%' && c2 != '$' && c2 != '\\') {
            return at - i;
        }
    }
    return -1;
}

// `[a-zA-Z0-9]`
static int SideAlnum(Str s, int i) {
    return MatchCp(s, i, IsAsciiAlnumCp);
}

// `[^%\$\\][a-zA-Z0-9]`
static int SideNotEscapeThenAlnum(Str s, int i) {
    if (i >= len(s)) {
        return -1;
    }
    int at = i;
    uint32_t cp = Utf8Next(s, &at);
    if (cp == '%' || cp == '$' || cp == '\\') {
        return -1;
    }
    if (at >= len(s) || !IsAsciiAlnumCp(Utf8At(s, at))) {
        return -1;
    }
    return at + 1 - i;
}

// `\p{CJK}` (the plain alternation: one char of any of the five scripts)
static int SideCjk(Str s, int i) {
    return MatchCp(s, i, IsCjk);
}

// `[\-+][\d]+`
static int SideSignedNumber(Str s, int i) {
    if (i >= len(s) || (s.s[i] != '-' && s.s[i] != '+')) {
        return -1;
    }
    int at = i + 1;
    int digits = 0;
    while (at < len(s) && s.s[at] >= '0' && s.s[at] <= '9') {
        at++;
        digits++;
    }
    return digits > 0 ? at - i : -1;
}

// `^[a-zA-Z0-9]`
static int SideStartAlnum(Str s, int i) {
    if (i != 0) {
        return -1;
    }
    return MatchCp(s, i, IsAsciiAlnumCp);
}

// `[0-9][%]`
static int SideDigitPercent(Str s, int i) {
    if (i + 1 < len(s) && s.s[i] >= '0' && s.s[i] <= '9' && s.s[i + 1] == '%') {
        return 2;
    }
    return -1;
}

// `[a-zA-Z0-9][+#]+`
static int SideAlnumPlusHash(Str s, int i) {
    if (i >= len(s) || !IsAsciiAlnumCp((uint8_t)s.s[i])) {
        return -1;
    }
    int at = i + 1;
    int n = 0;
    while (at < len(s) && (s.s[at] == '+' || s.s[at] == '#')) {
        at++;
        n++;
    }
    return n > 0 ? at - i : -1;
}

// `[\p{CJK_N}”’]`
static bool IsCjkOrRightQuoteCp(uint32_t cp) {
    return IsCjkClassCp(cp) || cp == 0x201D || cp == 0x2019; // ” ’
}
static int SideCjkOrRightQuote(Str s, int i) {
    return MatchCp(s, i, IsCjkOrRightQuoteCp);
}

// `[\p{CJK_N}\s（【「《“‘]` — the char allowed after a pipe/plus/dash.
static bool IsCjkSpaceOrLeftQuoteCp(uint32_t cp) {
    if (IsCjkClassCp(cp) || IsSpaceCp(cp)) {
        return true;
    }
    return cp == 0xFF08 || cp == 0x3010 || cp == 0x300C || cp == 0x300A ||
           cp == 0x201C || cp == 0x2018; // （ 【 「 《 “ ‘
}

// `[\p{CJK_N}\s）】」”’》]` — the char allowed before one.
static bool IsCjkSpaceOrCloseQuoteCp(uint32_t cp) {
    if (IsCjkClassCp(cp) || IsSpaceCp(cp)) {
        return true;
    }
    return cp == 0xFF09 || cp == 0x3011 || cp == 0x300D || cp == 0x201D ||
           cp == 0x2019 || cp == 0x300B; // ） 】 」 ” ’ 》
}

// `[\p{CJK_N}“‘]`
static bool IsCjkOrLeftQuoteCp(uint32_t cp) {
    return IsCjkClassCp(cp) || cp == 0x201C || cp == 0x2018; // “ ‘
}
static int SideCjkOrLeftQuote(Str s, int i) {
    return MatchCp(s, i, IsCjkOrLeftQuoteCp);
}

// `[\|+][\p{CJK_N}\s（【「《“‘]` and `[\-][…]`
static int SidePipeThenOpen(Str s, int i) {
    if (i >= len(s) || (s.s[i] != '|' && s.s[i] != '+')) {
        return -1;
    }
    int n = MatchCp(s, i + 1, IsCjkSpaceOrLeftQuoteCp);
    return n > 0 ? 1 + n : -1;
}
static int SideDashThenOpen(Str s, int i) {
    if (i >= len(s) || s.s[i] != '-') {
        return -1;
    }
    int n = MatchCp(s, i + 1, IsCjkSpaceOrLeftQuoteCp);
    return n > 0 ? 1 + n : -1;
}

// `[\p{CJK_N}\s）】」”’》][\|+]` and `[…][\-]`
static int SideCloseThenPipe(Str s, int i) {
    int n = MatchCp(s, i, IsCjkSpaceOrCloseQuoteCp);
    if (n <= 0 || i + n >= len(s) || (s.s[i + n] != '|' && s.s[i + n] != '+')) {
        return -1;
    }
    return n + 1;
}
static int SideCloseThenDash(Str s, int i) {
    int n = MatchCp(s, i, IsCjkSpaceOrCloseQuoteCp);
    if (n <= 0 || i + n >= len(s) || s.s[i + n] != '-') {
        return -1;
    }
    return n + 1;
}

// `[!]`
static int SideBang(Str s, int i) {
    return i < len(s) && s.s[i] == '!' ? 1 : -1;
}

// `[\[\(]` and `[\]\)]`
static int SideOpenBracket(Str s, int i) {
    return i < len(s) && (s.s[i] == '[' || s.s[i] == '(') ? 1 : -1;
}
static int SideCloseBracket(Str s, int i) {
    return i < len(s) && (s.s[i] == ']' || s.s[i] == ')') ? 1 : -1;
}

// `` `.+` `` — a backtick, at least one non-newline char, greedily to the
// last backtick in the line (regex greediness).
static int SideBacktickString(Str s, int i) {
    if (i >= len(s) || s.s[i] != '`') {
        return -1;
    }
    int last = -1;
    for (int j = i + 1; j < len(s) && s.s[j] != '\n'; j++) {
        if (s.s[j] == '`' && j > i + 1) {
            last = j;
        }
    }
    return last > 0 ? last + 1 - i : -1;
}

// `\$`
static int SideDollar(Str s, int i) {
    return i < len(s) && s.s[i] == '$' ? 1 : -1;
}

// `\w|\p{CJK}|`` ` `` — the char a fullwidth punctuation may absorb a space
// after (no-space-fullwidth).
static bool IsWordCjkOrBacktickCp(uint32_t cp) {
    return cp == '`' || IsWordCp(cp) || IsCjk(cp);
}
static int SideWordCjkOrBacktick(Str s, int i) {
    return MatchCp(s, i, IsWordCjkOrBacktickCp);
}

// `\w|\p{CJK}`
static bool IsWordOrCjkCp(uint32_t cp) {
    return IsWordCp(cp) || IsCjk(cp);
}
static int SideWordOrCjk(Str s, int i) {
    return MatchCp(s, i, IsWordOrCjkCp);
}

// `[，。、！？：；（）「」《》【】]`
static bool IsFullwidthPunctCp(uint32_t cp) {
    switch (cp) {
        case 0xFF0C: // ，
        case 0x3002: // 。
        case 0x3001: // 、
        case 0xFF01: // ！
        case 0xFF1F: // ？
        case 0xFF1A: // ：
        case 0xFF1B: // ；
        case 0xFF08: // （
        case 0xFF09: // ）
        case 0x300C: // 「
        case 0x300D: // 」
        case 0x300A: // 《
        case 0x300B: // 》
        case 0x3010: // 【
        case 0x3011: // 】
            return true;
        default:
            return false;
    }
}
static int SideFullwidthPunct(Str s, int i) {
    return MatchCp(s, i, IsFullwidthPunctCp);
}

// `[“”‘’]`
static bool IsFullwidthQuoteCp(uint32_t cp) {
    return cp == 0x201C || cp == 0x201D || cp == 0x2018 || cp == 0x2019;
}
static int SideFullwidthQuote(Str s, int i) {
    return MatchCp(s, i, IsFullwidthQuoteCp);
}

// ─── the Strategery driver ────────────────────────────────────────────────

// Strategery::add_space / remove_space: one replace_all pass. Writes the
// whole result into `out` and answers whether anything matched; a caller
// keeps the input when nothing did.
static bool PassAdd(Str in, SideFn one, SideFn other, StrBuilder* out) {
    bool changed = false;
    int i = 0;
    while (i < len(in)) {
        int n1 = one(in, i);
        if (n1 > 0) {
            int n2 = other(in, i + n1);
            if (n2 > 0) {
                out->Append(Str(in.s + i, n1));
                out->AppendChar(' ');
                out->Append(Str(in.s + i + n1, n2));
                i += n1 + n2;
                changed = true;
                continue;
            }
        }
        int step = Utf8Len(in, i);
        out->Append(Str(in.s + i, step));
        i += step;
    }
    return changed;
}

// `(one)[ ]+(other)` → `$1$2`.
static bool PassRemove(Str in, SideFn one, SideFn other, StrBuilder* out) {
    bool changed = false;
    int i = 0;
    while (i < len(in)) {
        int n1 = one(in, i);
        if (n1 > 0) {
            int sp = i + n1;
            while (sp < len(in) && in.s[sp] == ' ') {
                sp++;
            }
            if (sp > i + n1) {
                int n2 = other(in, sp);
                if (n2 > 0) {
                    out->Append(Str(in.s + i, n1));
                    out->Append(Str(in.s + sp, n2));
                    i = sp + n2;
                    changed = true;
                    continue;
                }
            }
        }
        int step = Utf8Len(in, i);
        out->Append(Str(in.s + i, step));
        i += step;
    }
    return changed;
}

// A rule's strategery list, expanded: with_reverse() is written out as a
// second pass with the sides swapped, which is what add_space does.
struct Pass {
    SideFn one;
    SideFn other;
    bool remove;
};

static bool RunPasses(Arena* a, Str in, const Pass* passes, int nPasses,
                      Str* out) {
    Str cur = in;
    bool changed = false;
    StrBuilder b;
    for (int p = 0; p < nPasses; p++) {
        b.Reset();
        bool hit = passes[p].remove
                       ? PassRemove(cur, passes[p].one, passes[p].other, &b)
                       : PassAdd(cur, passes[p].one, passes[p].other, &b);
        if (hit) {
            cur = base::StrDup(a, Str(b.els, b.len));
            changed = true;
        }
    }
    if (changed) {
        *out = cur;
    }
    return changed;
}

// ─── the rules ────────────────────────────────────────────────────────────

bool FormatSpaceWord(Arena* a, Str in, Str* out) {
    static const Pass kPasses[] = {
        {SideCjkWordOne, SideAlnum, false},
        {SideNotEscapeThenAlnum, SideCjk, false},
        // `(\p{CJK})([\-+][\d]+)` .with_reverse()
        {SideCjk, SideSignedNumber, false},
        {SideSignedNumber, SideCjk, false},
        {SideStartAlnum, SideCjk, false},
        {SideDigitPercent, SideCjk, false},
        {SideAlnumPlusHash, SideCjk, false},
    };
    return RunPasses(a, in, kPasses, 7, out);
}

bool FormatSpacePunctuation(Arena* a, Str in, Str* out) {
    static const Pass kPasses[] = {
        {SideCjkOrRightQuote, SidePipeThenOpen, false},
        {SideCloseThenPipe, SideCjkOrLeftQuote, false},
        {SideBang, SideCjk, false},
    };
    return RunPasses(a, in, kPasses, 3, out);
}

bool FormatSpaceBracket(Arena* a, Str in, Str* out) {
    static const Pass kPasses[] = {
        {SideCjk, SideOpenBracket, false},
        {SideCloseBracket, SideCjk, false},
    };
    return RunPasses(a, in, kPasses, 2, out);
}

bool FormatSpaceDash(Arena* a, Str in, Str* out) {
    static const Pass kPasses[] = {
        {SideCjkOrRightQuote, SideDashThenOpen, false},
        {SideCloseThenDash, SideCjkOrLeftQuote, false},
    };
    return RunPasses(a, in, kPasses, 2, out);
}

// `(`.+`)(\p{CJK})` needs backtracking a SideFn cannot express: the greedy
// backtick string must end at a backtick a CJK char follows. A pass of its
// own instead of the generic driver.
static bool PassBacktickThenCjk(Str in, StrBuilder* out) {
    bool changed = false;
    int i = 0;
    while (i < len(in)) {
        if (in.s[i] == '`') {
            int end = -1; // byte after the closing backtick
            for (int j = i + 2; j < len(in) && in.s[j] != '\n'; j++) {
                if (in.s[j] == '`' && j + 1 < len(in) &&
                    SideCjk(in, j + 1) > 0) {
                    end = j + 1;
                }
            }
            if (end > 0) {
                int n2 = SideCjk(in, end);
                out->Append(Str(in.s + i, end - i));
                out->AppendChar(' ');
                out->Append(Str(in.s + end, n2));
                i = end + n2;
                changed = true;
                continue;
            }
        }
        int step = Utf8Len(in, i);
        out->Append(Str(in.s + i, step));
        i += step;
    }
    return changed;
}

bool FormatSpaceBackticks(Arena* a, Str in, Str* out) {
    Str cur = in;
    bool changed = false;
    StrBuilder b;
    if (PassAdd(cur, SideCjk, SideBacktickString, &b)) {
        cur = base::StrDup(a, Str(b.els, b.len));
        changed = true;
    }
    b.Reset();
    if (PassBacktickThenCjk(cur, &b)) {
        cur = base::StrDup(a, Str(b.els, b.len));
        changed = true;
    }
    if (changed) {
        *out = cur;
    }
    return changed;
}

bool FormatSpaceDollar(Arena* a, Str in, Str* out) {
    static const Pass kPasses[] = {
        {SideCjk, SideDollar, false},
        {SideDollar, SideCjk, false},
    };
    return RunPasses(a, in, kPasses, 2, out);
}

bool FormatNoSpaceFullwidth(Arena* a, Str in, Str* out) {
    if (!HasCjk(in)) {
        return false;
    }
    static const Pass kPasses[] = {
        {SideWordCjkOrBacktick, SideFullwidthPunct, true},
        {SideFullwidthPunct, SideWordCjkOrBacktick, true},
    };
    return RunPasses(a, in, kPasses, 2, out);
}

bool FormatNoSpaceFullwidthQuote(Arena* a, Str in, Str* out) {
    if (!HasCjk(in)) {
        return false;
    }
    static const Pass kPasses[] = {
        {SideWordOrCjk, SideFullwidthQuote, true},
        {SideFullwidthQuote, SideWordOrCjk, true},
    };
    return RunPasses(a, in, kPasses, 2, out);
}

} // namespace autocorrect
