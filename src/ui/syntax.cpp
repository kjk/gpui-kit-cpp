#include "ui/syntax.h"

namespace gpui {

namespace component {

// ─── the language table ───────────────────────────────────────────────────
//
// One row per language: what a fence may call it, its keywords and type
// names, and which of the scanner's rules apply. Rust gets the same facts
// out of a tree-sitter grammar plus a highlights.scm per language; this is
// the part of that a scanner can carry.

struct SyntaxLangDef {
    // Space-separated fence infos. The first is the canonical name.
    const char* names;
    const char* keywords;
    const char* types;
    // "//" , "#", "--", ";" or nullptr.
    const char* lineComment;
    bool blockComment;   // /* .. */
    bool htmlComment;    // <!-- .. -->
    bool backtickString; // `template` / `command`
    bool tripleQuote;    // """docstring"""
    // A name that starts with a capital is a type: the convention Rust, Go,
    // Java, C# and TypeScript all follow.
    bool capsAreTypes;
    // #include / #define at the start of a line.
    bool hashPreproc;
    // A string before a ':' is a key (JSON), or a name before one is a
    // property (CSS, YAML).
    bool stringKeyProperty;
    bool identProperty;
    // Keywords match whatever their case (SQL).
    bool caseInsensitive;
    // Tags, attributes and text rather than statements.
    bool markup;
    // Prose with marks in it rather than either: headings, fences, links and
    // code spans. Upstream reads a tree-sitter markdown grammar with an
    // injection per fence; this is the part of that a scanner can carry.
    // Defaulted, so the fourteen entries that came before it stay as they
    // were written rather than each growing a `false` — gcc's
    // missing-field-initializers is what asks for one or the other.
    bool markdown = false;
};

// clang-format off
static const SyntaxLangDef kLangs[] = {
    {"cpp c cc cxx h hpp hxx c++ objc",
     "alignas alignof and asm break case catch class const consteval constexpr "
     "const_cast continue co_await co_return co_yield decltype default delete "
     "do dynamic_cast else enum explicit export extern false for friend goto "
     "if inline mutable namespace new noexcept nullptr operator private "
     "protected public register reinterpret_cast return sizeof static "
     "static_assert static_cast struct switch template this thread_local "
     "throw true try typedef typeid typename union using virtual volatile "
     "while NULL",
     "auto bool char char8_t char16_t char32_t double float int long short "
     "signed unsigned void wchar_t size_t ssize_t ptrdiff_t intptr_t uintptr_t "
     "int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t",
     "//", true, false, false, false, false, true, false, false, false, false},

    {"rust rs",
     "as async await break const continue crate dyn else enum extern false fn "
     "for if impl in let loop match mod move mut pub ref return self static "
     "struct super trait true type unsafe use where while",
     "bool char f32 f64 i8 i16 i32 i64 i128 isize str u8 u16 u32 u64 u128 "
     "usize",
     "//", true, false, false, false, true, false, false, false, false, false},

    {"js jsx mjs cjs javascript ts tsx typescript",
     "as async await break case catch class const continue debugger default "
     "delete do else export extends false finally for from function get if "
     "implements import in instanceof interface let new null of private "
     "protected public readonly return set static super switch this throw "
     "true try typeof undefined var void while with yield",
     "any bigint boolean never number object string symbol unknown",
     "//", true, false, true, false, true, false, false, false, false, false},

    {"python py",
     "and as assert async await break class continue def del elif else except "
     "finally for from global if import in is lambda match nonlocal not or "
     "pass raise return try while with yield",
     "bool bytes complex dict float frozenset int list object set str tuple",
     "#", false, false, false, true, true, false, false, false, false, false},

    {"go golang",
     "break case chan const continue default defer else fallthrough for func "
     "go goto if import interface map package range return select struct "
     "switch type var nil",
     "any bool byte complex64 complex128 error float32 float64 int int8 int16 "
     "int32 int64 rune string uint uint8 uint16 uint32 uint64 uintptr",
     "//", true, false, true, false, true, false, false, false, false, false},

    {"java kotlin kt",
     "abstract as assert break case catch class const continue default do "
     "else enum extends final finally for fun goto if implements import in "
     "instanceof interface internal is native new null object open operator "
     "override package private protected public return sealed static super "
     "switch synchronized this throw throws transient try val var when while "
     "false true",
     "boolean byte char double float int long short void Any Boolean Double "
     "Float Int Long Short String Unit",
     "//", true, false, false, false, true, false, false, false, false, false},

    {"csharp cs",
     "abstract as async await base break case catch checked class const "
     "continue default delegate do else enum event explicit extern false "
     "finally fixed for foreach get goto if implicit in interface internal is "
     "lock namespace new null operator out override params private protected "
     "public readonly ref return sealed set sizeof stackalloc static struct "
     "switch this throw true try typeof unchecked unsafe using var virtual "
     "volatile while yield",
     "bool byte char decimal double dynamic float int long object sbyte short "
     "string uint ulong ushort void",
     "//", true, false, false, false, true, false, false, false, false, false},

    {"sh bash zsh shell console",
     "alias break case cd continue do done elif else esac eval exec exit "
     "export fi for function if in local read return select set shift source "
     "then time trap unset until while",
     "", "#", false, false, true, false, false, false, false, false, false,
     false},

    {"json jsonc", "", "", "//", true, false, false, false, false, false, true,
     false, false, false},

    {"html xml svg xhtml vue", "", "", nullptr, false, true, false, false,
     false, false, false, false, false, true},

    {"css scss less", "important media import keyframes include mixin extend "
     "use", "", nullptr, true, false, false, false, false, false, false, true,
     false, false},

    {"sql",
     "add all alter and as asc between by case cast column create cross "
     "delete desc distinct drop else end exists from full group having if in "
     "index inner insert into is join key left like limit not null offset on "
     "or order outer primary references right select set table then true "
     "false union unique update values view when where with",
     "bigint blob boolean char date datetime decimal double float int integer "
     "numeric real serial smallint text time timestamp uuid varchar",
     "--", true, false, false, false, false, false, false, false, true, false},

    {"toml ini cfg conf", "true false", "", "#", false, false, false, false,
     false, false, false, true, false, false},

    {"yaml yml", "true false null yes no", "", "#", false, false, false, false,
     false, false, false, true, false, false},

    {"markdown md mdown markdn", "", "", nullptr, false, false, false, false,
     false, false, false, false, false, false, true},
};
// clang-format on

constexpr int kNLangs = (int)(sizeof(kLangs) / sizeof(kLangs[0]));

// Whether `list` — space-separated — holds `word`.
static bool SyntaxInList(const char* list, Str word, bool ci) {
    if (!list || len(word) <= 0) {
        return false;
    }
    for (const char* p = list; *p;) {
        const char* start = p;
        while (*p && *p != ' ') {
            p++;
        }
        int n = (int)(p - start);
        if (n == len(word)) {
            Str candidate(start, n);
            bool same = ci ? base::StrEqI(word, candidate)
                           : base::StrEq(word, candidate);
            if (same) {
                return true;
            }
        }
        while (*p == ' ') {
            p++;
        }
    }
    return false;
}

SyntaxLang SyntaxLangFor(Str info) {
    if (!info.s || len(info) <= 0) {
        return SyntaxLangNone;
    }
    // An HTML <code class="language-cpp"> and a fence whose info string
    // carries more than the name (cpp title=main.cpp) both need trimming.
    const char* kPrefixes[] = {"language-", "lang-"};
    for (const char* pre : kPrefixes) {
        int n = (int)strlen(pre);
        if (len(info) > n && StrEq(Str(info.s, n), Str(pre, n))) {
            info = Str(info.s + n, len(info) - n);
        }
    }
    int n = 0;
    while (n < len(info) && info.s[n] != ' ' && info.s[n] != '\t' &&
           info.s[n] != ',' && info.s[n] != '{') {
        n++;
    }
    if (n <= 0) {
        return SyntaxLangNone;
    }
    Str name(info.s, n);
    for (int i = 0; i < kNLangs; i++) {
        if (SyntaxInList(kLangs[i].names, name, true)) {
            return (SyntaxLang)i;
        }
    }
    return SyntaxLangNone;
}

Str SyntaxLangName(SyntaxLang lang) {
    if (lang < 0 || lang >= kNLangs) {
        return {};
    }
    const char* names = kLangs[lang].names;
    int len = 0;
    while (names[len] && names[len] != ' ') {
        len++;
    }
    return Str((char*)names, len);
}

// ─── the scanner ──────────────────────────────────────────────────────────

static bool SyntaxIsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static bool SyntaxIsDigit(char c) {
    return c >= '0' && c <= '9';
}

static bool SyntaxIsIdent(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           SyntaxIsDigit(c) || c == '_' || c == '$' || (unsigned char)c >= 0x80;
}

static bool SyntaxIsIdentStart(char c) {
    return SyntaxIsIdent(c) && !SyntaxIsDigit(c);
}

// A boolean or null literal, whatever the language spells it.
static bool SyntaxIsLiteralWord(Str w) {
    const char* kWords[] = {"true", "false", "null", "nil",     "None",
                            "True", "False", "NULL", "nullptr", "undefined"};
    for (const char* k : kWords) {
        int n = (int)strlen(k);
        if (StrEq(w, Str(k, n))) {
            return true;
        }
    }
    return false;
}

void SyntaxLexStart(SyntaxLexer* lx, SyntaxLang lang, Str src) {
    lx->def = (lang >= 0 && lang < kNLangs) ? &kLangs[lang] : nullptr;
    lx->src = src;
    lx->at = 0;
    lx->tok = SyntaxTok::Text;
    lx->text = {};
    lx->inTag = false;
    lx->tagName = false;
    lx->inFence = false;
    lx->linkDest = false;
}

static void SyntaxEmit(SyntaxLexer* lx, int start, SyntaxTok tok) {
    lx->tok = tok;
    lx->text = Str(lx->src.s + start, lx->at - start);
}

// The byte after the run of spaces at `at`, so a name can look at what
// follows it without the scan moving.
static int SyntaxSkipSpace(const SyntaxLexer* lx, int at) {
    while (at < len(lx->src) &&
           (lx->src.s[at] == ' ' || lx->src.s[at] == '\t')) {
        at++;
    }
    return at;
}

static bool SyntaxAt(const SyntaxLexer* lx, int at, const char* s) {
    int n = (int)strlen(s);
    return at + n <= len(lx->src) && StrEq(Str(lx->src.s + at, n), Str(s, n));
}

// A quoted run, to its closing quote. `escapes` is whether a backslash
// escapes the next byte — a shell's '..' and a raw string's do not.
static void SyntaxScanString(SyntaxLexer* lx, char quote, bool escapes) {
    lx->at++; // the opening quote
    while (lx->at < len(lx->src)) {
        char c = lx->src.s[lx->at];
        if (escapes && c == '\\' && lx->at + 1 < len(lx->src)) {
            lx->at += 2;
            continue;
        }
        lx->at++;
        if (c == quote) {
            return;
        }
        // An unterminated string ends at the line, so one stray quote does
        // not paint the rest of the block.
        if (c == '\n' && quote != '`') {
            return;
        }
    }
}

static void SyntaxScanTripleQuote(SyntaxLexer* lx, char quote) {
    lx->at += 3;
    while (lx->at < len(lx->src)) {
        if (lx->src.s[lx->at] == quote && SyntaxAt(lx, lx->at, "\"\"\"")) {
            lx->at += 3;
            return;
        }
        if (lx->src.s[lx->at] == '\'' && SyntaxAt(lx, lx->at, "'''")) {
            lx->at += 3;
            return;
        }
        lx->at++;
    }
}

// --- markdown ------------------------------------------------------------
//
// Line shapes first -- a fence, a heading, a quote, a list marker -- and then
// the marks inside a line: a code span, a link's text and its destination, an
// HTML tag. What is left is prose, and prose is Text.
//
// What a scanner cannot carry and upstream's grammar does: the emphasis marks
// are not coloured, because a decoration here is a colour and a wash where
// `**bold**` is a weight; and a fence's body is not scanned as the language
// its info string names, which would want a scanner inside a scanner.

static bool MdIsFenceLine(Str line) {
    int at = 0;
    while (at < len(line) && (line.s[at] == ' ' || line.s[at] == '\t')) {
        at++;
    }
    if (at + 3 > len(line)) {
        return false;
    }
    char c = line.s[at];
    return (c == '`' || c == '~') && line.s[at + 1] == c && line.s[at + 2] == c;
}

// One past the end of the line `at` is on -- the newline included, so the
// tokens partition the source.
static int MdLineEnd(Str s, int at) {
    while (at < len(s) && s.s[at] != '\n') {
        at++;
    }
    return at < len(s) ? at + 1 : at;
}

// `- `, `* `, `+ `, `1. ` or `1) ` at the start of a line, with whatever
// indent is in front of it. Answers where the marker ends.
static bool MdIsListMarker(Str s, int at, int* out) {
    int i = at;
    while (i < len(s) && (s.s[i] == ' ' || s.s[i] == '\t')) {
        i++;
    }
    int marker = i;
    if (i < len(s) && (s.s[i] == '-' || s.s[i] == '*' || s.s[i] == '+')) {
        i++;
    } else {
        int digits = 0;
        while (i < len(s) && s.s[i] >= '0' && s.s[i] <= '9') {
            i++;
            digits++;
        }
        if (digits == 0 || i >= len(s) || (s.s[i] != '.' && s.s[i] != ')')) {
            return false;
        }
        i++;
    }
    if (i == marker || i >= len(s) || (s.s[i] != ' ' && s.s[i] != '\t')) {
        return false;
    }
    *out = i;
    return true;
}

static bool SyntaxNextMarkdown(SyntaxLexer* lx) {
    Str s = lx->src;
    int start = lx->at;
    bool bol = lx->at == 0 || s.s[lx->at - 1] == '\n';
    char c = s.s[lx->at];

    // A fenced block is verbatim until the fence that closes it.
    if (lx->inFence) {
        int end = MdLineEnd(s, lx->at);
        Str line = Str(s.s + lx->at, end - lx->at);
        lx->at = end;
        if (MdIsFenceLine(line)) {
            lx->inFence = false;
        }
        SyntaxEmit(lx, start, SyntaxTok::Literal);
        return true;
    }
    // A destination reads as one only where a link's text just ended.
    if (lx->linkDest && c == '(') {
        lx->linkDest = false;
        int depth = 0;
        while (lx->at < len(s) && s.s[lx->at] != '\n') {
            char d = s.s[lx->at++];
            if (d == '(') {
                depth++;
            } else if (d == ')') {
                depth--;
                if (depth == 0) {
                    break;
                }
            }
        }
        SyntaxEmit(lx, start, SyntaxTok::Comment);
        return true;
    }
    lx->linkDest = false;

    if (bol) {
        int end = MdLineEnd(s, lx->at);
        Str line = Str(s.s + lx->at, end - lx->at);
        if (MdIsFenceLine(line)) {
            lx->inFence = true;
            lx->at = end;
            SyntaxEmit(lx, start, SyntaxTok::Literal);
            return true;
        }
        if (c == '#') {
            // An ATX heading is the whole line, hashes and all — `@title`.
            lx->at = end;
            SyntaxEmit(lx, start, SyntaxTok::Title);
            return true;
        }
        if (c == '>') {
            lx->at++;
            SyntaxEmit(lx, start, SyntaxTok::Comment);
            return true;
        }
        int afterMarker = 0;
        if (MdIsListMarker(s, lx->at, &afterMarker)) {
            lx->at = afterMarker;
            SyntaxEmit(lx, start, SyntaxTok::Keyword);
            return true;
        }
    }

    if (c == '`') {
        // A code span, to the run of backticks that closes it.
        int ticks = 0;
        while (lx->at < len(s) && s.s[lx->at] == '`') {
            lx->at++;
            ticks++;
        }
        while (lx->at < len(s) && s.s[lx->at] != '\n') {
            if (s.s[lx->at] != '`') {
                lx->at++;
                continue;
            }
            int run = 0;
            while (lx->at < len(s) && s.s[lx->at] == '`') {
                lx->at++;
                run++;
            }
            if (run >= ticks) {
                break;
            }
        }
        SyntaxEmit(lx, start, SyntaxTok::Literal);
        return true;
    }
    bool image = c == '!' && lx->at + 1 < len(s) && s.s[lx->at + 1] == '[';
    if (c == '[' || image) {
        int i = lx->at + (image ? 2 : 1);
        int depth = 1;
        while (i < len(s) && s.s[i] != '\n' && depth > 0) {
            if (s.s[i] == '[') {
                depth++;
            } else if (s.s[i] == ']') {
                depth--;
            }
            i++;
        }
        if (depth == 0) {
            lx->at = i;
            lx->linkDest = i < len(s) && s.s[i] == '(';
            SyntaxEmit(lx, start, SyntaxTok::Function);
            return true;
        }
    }
    if (c == '<') {
        int i = lx->at + 1;
        while (i < len(s) && s.s[i] != '>' && s.s[i] != '\n') {
            i++;
        }
        if (i < len(s) && s.s[i] == '>') {
            lx->at = i + 1;
            SyntaxEmit(lx, start, SyntaxTok::Tag);
            return true;
        }
    }
    // A newline is a token of its own, so what follows it starts a line and
    // the shapes above are reachable.
    if (c == '\n') {
        lx->at++;
        SyntaxEmit(lx, start, SyntaxTok::Text);
        return true;
    }
    // Prose, to the next byte that could start a mark.
    lx->at++;
    while (lx->at < len(s)) {
        char d = s.s[lx->at];
        if (d == '\n' || d == '`' || d == '[' || d == '<' || d == '!') {
            break;
        }
        lx->at++;
    }
    SyntaxEmit(lx, start, SyntaxTok::Text);
    return true;
}

// <tag attr="value">, the one shape a scanner can follow without a parse.
static bool SyntaxNextMarkup(SyntaxLexer* lx) {
    const SyntaxLangDef* d = (const SyntaxLangDef*)lx->def;
    int start = lx->at;
    char c = lx->src.s[lx->at];
    if (SyntaxAt(lx, lx->at, "<!--")) {
        lx->at += 4;
        while (lx->at < len(lx->src) && !SyntaxAt(lx, lx->at, "-->")) {
            lx->at++;
        }
        lx->at = lx->at < len(lx->src) ? lx->at + 3 : len(lx->src);
        SyntaxEmit(lx, start, SyntaxTok::Comment);
        return true;
    }
    if (!lx->inTag) {
        if (c == '<') {
            lx->at++;
            if (lx->at < len(lx->src) &&
                (lx->src.s[lx->at] == '/' || lx->src.s[lx->at] == '!' ||
                 lx->src.s[lx->at] == '?')) {
                lx->at++;
            }
            lx->inTag = true;
            lx->tagName = true;
            SyntaxEmit(lx, start, SyntaxTok::Text);
            return true;
        }
        while (lx->at < len(lx->src) && lx->src.s[lx->at] != '<') {
            lx->at++;
        }
        SyntaxEmit(lx, start, SyntaxTok::Text);
        return true;
    }
    if (c == '>') {
        lx->at++;
        lx->inTag = false;
        lx->tagName = false;
        SyntaxEmit(lx, start, SyntaxTok::Text);
        return true;
    }
    if (c == '"' || c == '\'') {
        SyntaxScanString(lx, c, false);
        SyntaxEmit(lx, start, SyntaxTok::String);
        return true;
    }
    if (SyntaxIsIdentStart(c) || c == '-' || c == ':') {
        // The name right after the `<` is the tag; every other name inside
        // the tag is an attribute.
        bool tag = lx->tagName;
        lx->tagName = false;
        while (lx->at < len(lx->src) &&
               (SyntaxIsIdent(lx->src.s[lx->at]) || lx->src.s[lx->at] == '-' ||
                lx->src.s[lx->at] == ':')) {
            lx->at++;
        }
        SyntaxEmit(lx, start, tag ? SyntaxTok::Tag : SyntaxTok::Attribute);
        return true;
    }
    (void)d;
    lx->at++;
    SyntaxEmit(lx, start, SyntaxTok::Text);
    return true;
}

bool SyntaxLexNext(SyntaxLexer* lx) {
    if (!lx->src.s || lx->at >= len(lx->src)) {
        return false;
    }
    const SyntaxLangDef* d = (const SyntaxLangDef*)lx->def;
    if (!d) {
        lx->at = len(lx->src);
        lx->tok = SyntaxTok::Text;
        lx->text = lx->src;
        return true;
    }
    if (d->markup) {
        return SyntaxNextMarkup(lx);
    }
    if (d->markdown) {
        return SyntaxNextMarkdown(lx);
    }

    int start = lx->at;
    char c = lx->src.s[lx->at];

    if (SyntaxIsSpace(c)) {
        while (lx->at < len(lx->src) && SyntaxIsSpace(lx->src.s[lx->at])) {
            lx->at++;
        }
        SyntaxEmit(lx, start, SyntaxTok::Text);
        return true;
    }
    if (d->lineComment && SyntaxAt(lx, lx->at, d->lineComment)) {
        while (lx->at < len(lx->src) && lx->src.s[lx->at] != '\n') {
            lx->at++;
        }
        SyntaxEmit(lx, start, SyntaxTok::Comment);
        return true;
    }
    if (d->blockComment && SyntaxAt(lx, lx->at, "/*")) {
        lx->at += 2;
        while (lx->at < len(lx->src) && !SyntaxAt(lx, lx->at, "*/")) {
            lx->at++;
        }
        lx->at = lx->at < len(lx->src) ? lx->at + 2 : len(lx->src);
        SyntaxEmit(lx, start, SyntaxTok::Comment);
        return true;
    }
    if (d->htmlComment && SyntaxAt(lx, lx->at, "<!--")) {
        lx->at += 4;
        while (lx->at < len(lx->src) && !SyntaxAt(lx, lx->at, "-->")) {
            lx->at++;
        }
        lx->at = lx->at < len(lx->src) ? lx->at + 3 : len(lx->src);
        SyntaxEmit(lx, start, SyntaxTok::Comment);
        return true;
    }
    if (d->tripleQuote &&
        (SyntaxAt(lx, lx->at, "\"\"\"") || SyntaxAt(lx, lx->at, "'''"))) {
        SyntaxScanTripleQuote(lx, c);
        SyntaxEmit(lx, start, SyntaxTok::String);
        return true;
    }
    if (c == '"' || c == '\'' || (c == '`' && d->backtickString)) {
        // A shell's '..' takes no escapes; everything else does.
        SyntaxScanString(
            lx, c, !(c == '\'' && d->lineComment && d->lineComment[0] == '#'));
        SyntaxTok tok = SyntaxTok::String;
        if (d->stringKeyProperty) {
            // JSON: a string before a ':' is the object's key, which the
            // theme paints as a property rather than as a string.
            int next = SyntaxSkipSpace(lx, lx->at);
            if (next < len(lx->src) && lx->src.s[next] == ':') {
                tok = SyntaxTok::Property;
            }
        }
        SyntaxEmit(lx, start, tok);
        return true;
    }
    if (d->hashPreproc && c == '#') {
        // #include, #define: the directive, with the line after it scanned
        // as usual so a header name still comes out a string.
        bool atLineStart = true;
        for (int i = start - 1; i >= 0 && lx->src.s[i] != '\n'; i--) {
            if (!SyntaxIsSpace(lx->src.s[i])) {
                atLineStart = false;
                break;
            }
        }
        if (atLineStart) {
            lx->at++;
            while (lx->at < len(lx->src) && SyntaxIsIdent(lx->src.s[lx->at])) {
                lx->at++;
            }
            SyntaxEmit(lx, start, SyntaxTok::Keyword);
            return true;
        }
    }
    if (SyntaxIsDigit(c) || (c == '.' && lx->at + 1 < len(lx->src) &&
                             SyntaxIsDigit(lx->src.s[lx->at + 1]))) {
        while (lx->at < len(lx->src) &&
               (SyntaxIsIdent(lx->src.s[lx->at]) || lx->src.s[lx->at] == '.')) {
            lx->at++;
        }
        SyntaxEmit(lx, start, SyntaxTok::Number);
        return true;
    }
    if (SyntaxIsIdentStart(c)) {
        while (lx->at < len(lx->src) && SyntaxIsIdent(lx->src.s[lx->at])) {
            lx->at++;
        }
        Str word(lx->src.s + start, lx->at - start);
        SyntaxTok tok = SyntaxTok::Text;
        if (SyntaxIsLiteralWord(word) ||
            SyntaxInList("true false", word, d->caseInsensitive)) {
            tok = SyntaxTok::Boolean;
        } else if (SyntaxInList(d->types, word, d->caseInsensitive)) {
            tok = SyntaxTok::Type;
        } else if (SyntaxInList(d->keywords, word, d->caseInsensitive)) {
            tok = SyntaxTok::Keyword;
        } else {
            int next = SyntaxSkipSpace(lx, lx->at);
            char after = next < len(lx->src) ? lx->src.s[next] : 0;
            if (after == '(') {
                tok = SyntaxTok::Function;
            } else if (d->identProperty && (after == ':' || after == '=')) {
                tok = SyntaxTok::Property;
            } else if (d->capsAreTypes && word.s[0] >= 'A' &&
                       word.s[0] <= 'Z') {
                tok = SyntaxTok::Type;
            }
        }
        SyntaxEmit(lx, start, tok);
        return true;
    }
    lx->at++;
    SyntaxEmit(lx, start, SyntaxTok::Text);
    return true;
}

// ─── colors ───────────────────────────────────────────────────────────────

// theme/default-theme.json, the `syntax` block of "Default Light" and
// "Default Dark" — the two HighlightTheme::default_* tables.
struct SyntaxColorRow {
    SyntaxTok tok;
    Rgba light;
    Rgba dark;
};

static const SyntaxColorRow kColors[] = {
    {SyntaxTok::Keyword, {0x04, 0x33, 0xff, 0xff}, {0xc2, 0x8b, 0x12, 0xff}},
    {SyntaxTok::Type, {0x6f, 0x42, 0xc1, 0xff}, {0xc7, 0x58, 0x28, 0xff}},
    {SyntaxTok::Function, {0x00, 0x00, 0xa2, 0xff}, {0xfd, 0xd8, 0x88, 0xff}},
    {SyntaxTok::Property, {0x33, 0x33, 0x33, 0xff}, {0xca, 0xcc, 0xca, 0xff}},
    {SyntaxTok::String, {0x03, 0x6a, 0x07, 0xff}, {0x62, 0xba, 0x46, 0xff}},
    {SyntaxTok::Number, {0x04, 0x33, 0xff, 0xff}, {0xe1, 0xd7, 0x97, 0xff}},
    {SyntaxTok::Boolean, {0xc5, 0x06, 0x0b, 0xff}, {0xe1, 0xd7, 0x97, 0xff}},
    {SyntaxTok::Comment, {0x00, 0x7f, 0xff, 0xff}, {0x9e, 0x9e, 0x9e, 0xff}},
    {SyntaxTok::Tag, {0x04, 0x33, 0xff, 0xff}, {0xb5, 0xaf, 0x9a, 0xff}},
    {SyntaxTok::Attribute, {0x95, 0x79, 0x31, 0xff}, {0xe7, 0xcb, 0x8f, 0xff}},
    // default-theme.json `title` / `text.literal` (and `text.code.span`).
    {SyntaxTok::Title, {0x04, 0x33, 0xff, 0xff}, {0xcc, 0x9e, 0x00, 0xff}},
    {SyntaxTok::Literal, {0x6f, 0x42, 0xc1, 0xff}, {0xa3, 0xe0, 0x9f, 0xff}},
};

Rgba SyntaxTokColor(SyntaxTok tok, ThemeMode mode, Rgba fallback) {
    for (const SyntaxColorRow& row : kColors) {
        if (row.tok == tok) {
            return mode == ThemeMode::Dark ? row.dark : row.light;
        }
    }
    return fallback;
}

} // namespace component
} // namespace gpui
