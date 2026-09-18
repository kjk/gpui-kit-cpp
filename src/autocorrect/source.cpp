/* The source-code grammars (grammar/rust.pest, javascript.pest, c.pest,
   python.pest, ruby.pest, go.pest, sql.pest, css.pest, conf.pest,
   java.pest, csharp.pest, swift.pest, kotlin.pest, scala.pest, dart.pest,
   elixir.pest, php.pest, objective_c.pest, json.pest, yaml.pest) as
   scanners.

   Part of the C++ port of the `autocorrect` crate 2.14.2 (see
   src/autocorrect/readme.md).

   They all share one shape: at every position pest first tries the implicit
   COMMENT rule, then the `line` alternatives (regexps and other ignored
   forms, then strings), and otherwise consumes one `other` char. Comments
   and strings are corrected (and a comment may carry an
   `autocorrect-enable/disable` toggle); regexps, keys and includes pass
   through. The pest grammars do not honour backslash escapes inside
   strings, and neither does this — a `\"` ends a Rust string here exactly
   as it does in the crate. */

#include "autocorrect/internal.h"

#include <string.h>

namespace autocorrect {

// ─── match helpers ────────────────────────────────────────────────────────

static int LitLen(Str s, int i, const char* lit) {
    Str literal = Str(lit);
    if (i + len(literal) > len(s) ||
        !StrEq(Str(s.s + i, len(literal)), literal)) {
        return -1;
    }
    return len(literal);
}

// `prefix ~ (!NEWLINE ~ ANY)*`
static int MatchLineComment(Str s, int i, const char* prefix) {
    int n = LitLen(s, i, prefix);
    if (n < 0) {
        return -1;
    }
    int at = i + n;
    while (at < len(s) && s.s[at] != '\n') {
        at++;
    }
    return at - i;
}

// `open ~ (!close ~ ANY)* ~ close` — unterminated fails, like the PEG.
static int MatchBlock(Str s, int i, const char* open, const char* close) {
    int n = LitLen(s, i, open);
    if (n < 0) {
        return -1;
    }
    Str closing = Str(close);
    int closeLen = len(closing);
    for (int at = i + n; at + closeLen <= len(s); at++) {
        if (StrEq(Str(s.s + at, closeLen), closing)) {
            return at + closeLen - i;
        }
    }
    return -1;
}

// `q ~ (!(NEWLINE | q) ~ ANY)* ~ q`
static int MatchSingleLine(Str s, int i, char q) {
    if (i >= len(s) || s.s[i] != q) {
        return -1;
    }
    for (int at = i + 1; at < len(s) && s.s[at] != '\n'; at++) {
        if (s.s[at] == q) {
            return at + 1 - i;
        }
    }
    return -1;
}

// `q ~ (!q ~ ANY)* ~ q` — may span lines.
static int MatchMultiLine(Str s, int i, char q) {
    if (i >= len(s) || s.s[i] != q) {
        return -1;
    }
    for (int at = i + 1; at < len(s); at++) {
        if (s.s[at] == q) {
            return at + 1 - i;
        }
    }
    return -1;
}

// `fn(  ~ " "* ~ inner_string ~ (!")" ~ ANY)* ~ )` — the Regex(...) /
// regexp.MustCompile(...) shapes every grammar ignores. `matchString`
// answers the string form the language uses.
using StringFn = int (*)(Str, int);

static int MatchCallWithString(Str s, int i, const char* fn,
                               StringFn matchString) {
    int n = LitLen(s, i, fn);
    if (n < 0) {
        return -1;
    }
    int at = i + n;
    while (at < len(s) && s.s[at] == ' ') {
        at++;
    }
    int sn = matchString(s, at);
    if (sn < 0) {
        return -1;
    }
    at += sn;
    while (at < len(s) && s.s[at] != ')') {
        at++;
    }
    if (at >= len(s)) {
        return -1;
    }
    return at + 1 - i;
}

// ─── the scanner driver ───────────────────────────────────────────────────

// One `line` alternative: matched span is emitted under `rule`, or ignored
// when rule is null.
struct Alt {
    int (*match)(Str, int);
    const char* rule;
};

static void ScanAlts(Results* res, Str raw, const Alt* alts, int nAlts) {
    int ignoreStart = 0;
    int i = 0;
    while (i < len(raw)) {
        int matched = -1;
        const Alt* hit = nullptr;
        for (int k = 0; k < nAlts; k++) {
            matched = alts[k].match(raw, i);
            if (matched > 0) {
                hit = &alts[k];
                break;
            }
        }
        if (!hit) {
            i++;
            continue;
        }
        if (hit->rule) {
            if (i > ignoreStart) {
                EmitIgnore(res, Str(raw.s + ignoreStart, i - ignoreStart));
            }
            EmitText(res, Str(hit->rule), Str(raw.s + i, matched));
            ignoreStart = i + matched;
        }
        // An ignored form (a regexp, an include) just extends the run.
        i += matched;
    }
    if (len(raw) > ignoreStart) {
        EmitIgnore(res, Str(raw.s + ignoreStart, len(raw) - ignoreStart));
    }
}

// ─── the languages ────────────────────────────────────────────────────────

static int CppLineComment(Str s, int i) {
    return MatchLineComment(s, i, "//");
}
static int CppBlockComment(Str s, int i) {
    return MatchBlock(s, i, "/*", "*/");
}
static int HashLineComment(Str s, int i) {
    return MatchLineComment(s, i, "#");
}
static int DoubleQuoteSingleLine(Str s, int i) {
    return MatchSingleLine(s, i, '"');
}
static int SingleQuoteSingleLine(Str s, int i) {
    return MatchSingleLine(s, i, '\'');
}

// rust (also zig): regexp `r"…"`, string `"…"` (may span lines) or raw
// `r#…#"…"#…#`.
static int RustRegexp(Str s, int i) {
    if (LitLen(s, i, "r\"") < 0) {
        return -1;
    }
    int n = MatchSingleLine(s, i + 1, '"');
    return n > 0 ? 1 + n : -1;
}

static int RustString(Str s, int i) {
    int n = MatchMultiLine(s, i, '"');
    if (n > 0) {
        return n;
    }
    // "r" ~ PUSH("#"*) ~ "\"" ~ (!PEEK ~ ANY)* ~ "\"" ~ POP
    if (i >= len(s) || s.s[i] != 'r') {
        return -1;
    }
    int hashes = 0;
    int at = i + 1;
    while (at < len(s) && s.s[at] == '#') {
        at++;
        hashes++;
    }
    if (hashes == 0 || at >= len(s) || s.s[at] != '"') {
        return -1;
    }
    at++;
    // The PEG stops the inner text at the first run of hashes — a raw
    // string containing '#' fails to match, faithfully.
    for (; at < len(s); at++) {
        bool atHashes = at + hashes <= len(s);
        for (int h = 0; atHashes && h < hashes; h++) {
            atHashes = s.s[at + h] == '#';
        }
        if (atHashes) {
            break;
        }
    }
    if (at >= len(s) || s.s[at] != '"') {
        return -1;
    }
    at++;
    for (int h = 0; h < hashes; h++) {
        if (at >= len(s) || s.s[at] != '#') {
            return -1;
        }
        at++;
    }
    return at - i;
}

void ScanRust(Results* res, Str raw) {
    static const Alt kAlts[] = {
        {CppLineComment, "COMMENT"},
        {CppBlockComment, "COMMENT"},
        {RustRegexp, nullptr},
        {RustString, "string"},
    };
    ScanAlts(res, raw, kAlts, 4);
}

// c: `#include "…"` stays, `"…"` is corrected.
static int CInclude(Str s, int i) {
    int n = LitLen(s, i, "#include");
    if (n < 0) {
        return -1;
    }
    int at = i + n;
    int spaces = 0;
    while (at < len(s) && s.s[at] == ' ') {
        at++;
        spaces++;
    }
    if (spaces == 0) {
        return -1;
    }
    int sn = MatchSingleLine(s, at, '"');
    return sn > 0 ? at + sn - i : -1;
}

void ScanC(Results* res, Str raw) {
    static const Alt kAlts[] = {
        {CppLineComment, "COMMENT"},
        {CppBlockComment, "COMMENT"},
        {CInclude, nullptr},
        {DoubleQuoteSingleLine, "string"},
    };
    ScanAlts(res, raw, kAlts, 4);
}

// objective_c: only `@"…"` is a string; NSLocalizedString(@"…" and
// WithKey:@"…" arguments are ignored.
static int ObjcString(Str s, int i) {
    if (LitLen(s, i, "@\"") < 0) {
        return -1;
    }
    int n = MatchSingleLine(s, i + 1, '"');
    return n > 0 ? 1 + n : -1;
}

static int ObjcSkipBlank(Str s, int i) {
    while (i < len(s) && ((uint8_t)s.s[i] == ' ' || s.s[i] == '\t' ||
                          s.s[i] == '\n' || s.s[i] == '\r')) {
        i++;
    }
    return i;
}

static int ObjcIgnoreString(Str s, int i) {
    static const char* const kMethods[] = {"NSRegularExpression",
                                           "NSLocalizedString", "Match"};
    for (const char* m : kMethods) {
        int n = LitLen(s, i, m);
        if (n < 0) {
            continue;
        }
        int at = i + n;
        if (at >= len(s) || s.s[at] != '(') {
            continue;
        }
        at = ObjcSkipBlank(s, at + 1);
        int sn = ObjcString(s, at);
        if (sn > 0) {
            return at + sn - i;
        }
    }
    static const char* const kArgs[] = {"WithPattern", "WithKey"};
    for (const char* arg : kArgs) {
        int n = LitLen(s, i, arg);
        if (n < 0) {
            continue;
        }
        int at = i + n;
        if (at >= len(s) || s.s[at] != ':') {
            continue;
        }
        at = ObjcSkipBlank(s, at + 1);
        int sn = ObjcString(s, at);
        if (sn > 0) {
            return at + sn - i;
        }
    }
    return -1;
}

void ScanObjectiveC(Results* res, Str raw) {
    static const Alt kAlts[] = {
        {CppLineComment, "COMMENT"},
        {ObjcIgnoreString, nullptr},
        {ObjcString, "string"},
    };
    ScanAlts(res, raw, kAlts, 3);
}

// python: `#` comments, `'''…'''` doc comments, r-strings and
// re.compile(…) ignored.
static int PyTripleQuoteComment(Str s, int i) {
    return MatchBlock(s, i, "'''", "'''");
}
static int PyString(Str s, int i) {
    int n = MatchSingleLine(s, i, '\'');
    if (n > 0) {
        return n;
    }
    n = MatchBlock(s, i, "\"\"\"", "\"\"\"");
    if (n > 0) {
        // `"""…"""` then `"`+: extra closing quotes belong to the string.
        int at = i + n;
        while (at < len(s) && s.s[at] == '"') {
            at++;
        }
        return at - i;
    }
    return MatchSingleLine(s, i, '"');
}
static int PyRegexp(Str s, int i) {
    if (i < len(s) && s.s[i] == 'r') {
        int n = PyString(s, i + 1);
        if (n > 0) {
            return 1 + n;
        }
    }
    return MatchCallWithString(s, i, "compile(", PyString);
}

void ScanPython(Results* res, Str raw) {
    static const Alt kAlts[] = {
        {HashLineComment, "COMMENT"},
        {PyTripleQuoteComment, "COMMENT"},
        {PyRegexp, nullptr},
        {PyString, "string"},
    };
    ScanAlts(res, raw, kAlts, 4);
}

// ruby (also sh, crystal): `#` comments; strings before regexps.
static int RubyString(Str s, int i) {
    int n = MatchSingleLine(s, i, '\'');
    return n > 0 ? n : MatchSingleLine(s, i, '"');
}
static int RubyRegexp(Str s, int i) {
    int n = MatchSingleLine(s, i, '/');
    if (n > 0) {
        return n;
    }
    n = LitLen(s, i, "%r{");
    if (n > 0) {
        for (int at = i + n; at < len(s) && s.s[at] != '\n'; at++) {
            if (s.s[at] == '}') {
                return at + 1 - i;
            }
        }
        return -1;
    }
    return MatchCallWithString(s, i, "Regexp.new(", RubyString);
}

void ScanRuby(Results* res, Str raw) {
    static const Alt kAlts[] = {
        {HashLineComment, "COMMENT"},
        {CppBlockComment, "COMMENT"},
        {RubyString, "string"},
        {RubyRegexp, nullptr},
    };
    ScanAlts(res, raw, kAlts, 4);
}

// go: `…%s…` verbs make a string pass through, regexp./time. calls ignored.
static int GoString(Str s, int i) {
    char q = i < len(s) ? s.s[i] : 0;
    if (q != '"' && q != '`') {
        return -1;
    }
    for (int at = i + 1; at < len(s); at++) {
        char c = s.s[at];
        if (c == q) {
            return at + 1 - i;
        }
        if (q == '"' && c == '\n') {
            return -1;
        }
        if (c == '%' && at + 1 < len(s) &&
            (s.s[at + 1] == 's' || s.s[at + 1] == 'q' || s.s[at + 1] == 'v')) {
            return -1;
        }
    }
    return -1;
}

static int GoCall(Str s, int i, const char* pkg) {
    int n = LitLen(s, i, pkg);
    if (n < 0) {
        return -1;
    }
    int at = i + n;
    int letters = 0;
    while (at < len(s) && ((s.s[at] >= 'a' && s.s[at] <= 'z') ||
                           (s.s[at] >= 'A' && s.s[at] <= 'Z'))) {
        at++;
        letters++;
    }
    if (letters == 0 || at >= len(s) || s.s[at] != '(') {
        return -1;
    }
    at++;
    while (at < len(s) && s.s[at] == ' ') {
        at++;
    }
    int sn = GoString(s, at);
    if (sn < 0) {
        return -1;
    }
    at += sn;
    while (at < len(s) && s.s[at] != ')') {
        at++;
    }
    return at < len(s) ? at + 1 - i : -1;
}

static int GoRegexp(Str s, int i) {
    return GoCall(s, i, "regexp.");
}
static int GoTimeParse(Str s, int i) {
    return GoCall(s, i, "time.");
}

void ScanGo(Results* res, Str raw) {
    static const Alt kAlts[] = {
        {CppLineComment, "COMMENT"}, {CppBlockComment, "COMMENT"},
        {GoRegexp, nullptr},         {GoTimeParse, nullptr},
        {GoString, "string"},
    };
    ScanAlts(res, raw, kAlts, 5);
}

// sql
static int SqlLineComment(Str s, int i) {
    return MatchLineComment(s, i, "--");
}

void ScanSql(Results* res, Str raw) {
    static const Alt kAlts[] = {
        {SqlLineComment, "COMMENT"},
        {CppBlockComment, "COMMENT"},
        {SingleQuoteSingleLine, "string"},
    };
    ScanAlts(res, raw, kAlts, 3);
}

// css / conf: comments only.
void ScanCss(Results* res, Str raw) {
    static const Alt kAlts[] = {
        {CppLineComment, "COMMENT"},
        {CppBlockComment, "COMMENT"},
    };
    ScanAlts(res, raw, kAlts, 2);
}

void ScanConf(Results* res, Str raw) {
    static const Alt kAlts[] = {
        {HashLineComment, "COMMENT"},
        {CppBlockComment, "COMMENT"},
    };
    ScanAlts(res, raw, kAlts, 2);
}

// java (also proto): `"""…"""` and `"…"`; Pattern.…(…) ignored.
static int JavaString(Str s, int i) {
    int n = MatchBlock(s, i, "\"\"\"", "\"\"\"");
    return n > 0 ? n : MatchSingleLine(s, i, '"');
}
static int JavaRegexp(Str s, int i) {
    return GoCall(s, i, "Pattern.");
}

void ScanJava(Results* res, Str raw) {
    static const Alt kAlts[] = {
        {CppLineComment, "COMMENT"},
        {CppBlockComment, "COMMENT"},
        {JavaRegexp, nullptr},
        {JavaString, "string"},
    };
    ScanAlts(res, raw, kAlts, 4);
}

// csharp: `@"…"` spans lines, `"…"` and `$"…"` do not; Regex(…) ignored.
static int CsString(Str s, int i) {
    if (LitLen(s, i, "@\"") >= 0) {
        int n = MatchMultiLine(s, i + 1, '"');
        return n > 0 ? 1 + n : -1;
    }
    if (LitLen(s, i, "$\"") >= 0) {
        int n = MatchSingleLine(s, i + 1, '"');
        return n > 0 ? 1 + n : -1;
    }
    return MatchSingleLine(s, i, '"');
}
static int CsRegexp(Str s, int i) {
    return MatchCallWithString(s, i, "Regex(", CsString);
}

void ScanCsharp(Results* res, Str raw) {
    static const Alt kAlts[] = {
        {CppLineComment, "COMMENT"},
        {CppBlockComment, "COMMENT"},
        {CsRegexp, nullptr},
        {CsString, "string"},
    };
    ScanAlts(res, raw, kAlts, 4);
}

// swift: `pattern:`/`key:` arguments and NSRegularExpression(… ignored.
static int SwiftString(Str s, int i) {
    int n = MatchBlock(s, i, "\"\"\"", "\"\"\"");
    return n > 0 ? n : MatchSingleLine(s, i, '"');
}
static int SwiftIgnoreString(Str s, int i) {
    static const char* const kMethods[] = {"NSRegularExpression",
                                           "NSLocalizedString", "Match"};
    for (const char* m : kMethods) {
        int n = LitLen(s, i, m);
        if (n < 0) {
            continue;
        }
        int at = i + n;
        if (at >= len(s) || s.s[at] != '(') {
            continue;
        }
        at = ObjcSkipBlank(s, at + 1);
        int sn = SwiftString(s, at);
        if (sn > 0) {
            return at + sn - i;
        }
    }
    static const char* const kArgs[] = {"pattern", "key"};
    for (const char* arg : kArgs) {
        int n = LitLen(s, i, arg);
        if (n < 0) {
            continue;
        }
        int at = i + n;
        if (at >= len(s) || s.s[at] != ':') {
            continue;
        }
        at = ObjcSkipBlank(s, at + 1);
        int sn = SwiftString(s, at);
        if (sn > 0) {
            return at + sn - i;
        }
    }
    return -1;
}

void ScanSwift(Results* res, Str raw) {
    static const Alt kAlts[] = {
        {CppLineComment, "COMMENT"},
        {CppBlockComment, "COMMENT"},
        {SwiftIgnoreString, nullptr},
        {SwiftString, "string"},
    };
    ScanAlts(res, raw, kAlts, 4);
}

// kotlin (also gradle): a string followed by .toRegex() is a regexp.
static int KotlinString(Str s, int i) {
    int n = MatchBlock(s, i, "\"\"\"", "\"\"\"");
    return n > 0 ? n : MatchSingleLine(s, i, '"');
}
static int KotlinRegexp(Str s, int i) {
    int n = MatchCallWithString(s, i, "Regex(", KotlinString);
    if (n > 0) {
        return n;
    }
    n = KotlinString(s, i);
    if (n > 0) {
        int suffix = LitLen(s, i + n, ".toRegex()");
        if (suffix > 0) {
            return n + suffix;
        }
    }
    return -1;
}

void ScanKotlin(Results* res, Str raw) {
    static const Alt kAlts[] = {
        {CppLineComment, "COMMENT"},
        {CppBlockComment, "COMMENT"},
        {KotlinRegexp, nullptr},
        {KotlinString, "string"},
    };
    ScanAlts(res, raw, kAlts, 4);
}

// scala: s"…" interpolations ignored, `…".r` regexps ignored.
static int ScalaString(Str s, int i) {
    int n = MatchBlock(s, i, "\"\"\"", "\"\"\"");
    return n > 0 ? n : MatchSingleLine(s, i, '"');
}
static int ScalaStringLiteral(Str s, int i) {
    if (i >= len(s) || s.s[i] != 's') {
        return -1;
    }
    int n = ScalaString(s, i + 1);
    return n > 0 ? 1 + n : -1;
}
static int ScalaRegexp(Str s, int i) {
    int n = MatchCallWithString(s, i, "Regex(", ScalaString);
    if (n > 0) {
        return n;
    }
    n = ScalaString(s, i);
    if (n > 0 && LitLen(s, i + n, ".r") > 0) {
        return n + 2;
    }
    return -1;
}

void ScanScala(Results* res, Str raw) {
    static const Alt kAlts[] = {
        {CppLineComment, "COMMENT"}, {CppBlockComment, "COMMENT"},
        {ScalaRegexp, nullptr},      {ScalaStringLiteral, nullptr},
        {ScalaString, "string"},
    };
    ScanAlts(res, raw, kAlts, 5);
}

// dart: r"…" raw strings ignored.
static int DartString(Str s, int i) {
    int n = MatchBlock(s, i, "'''", "'''");
    if (n > 0) {
        return n;
    }
    n = MatchSingleLine(s, i, '\'');
    if (n > 0) {
        return n;
    }
    n = MatchBlock(s, i, "\"\"\"", "\"\"\"");
    return n > 0 ? n : MatchSingleLine(s, i, '"');
}
static int DartRegexp(Str s, int i) {
    if (i >= len(s) || s.s[i] != 'r') {
        return -1;
    }
    int n = DartString(s, i + 1);
    return n > 0 ? 1 + n : -1;
}

void ScanDart(Results* res, Str raw) {
    static const Alt kAlts[] = {
        {CppLineComment, "COMMENT"},
        {CppBlockComment, "COMMENT"},
        {DartRegexp, nullptr},
        {DartString, "string"},
    };
    ScanAlts(res, raw, kAlts, 4);
}

// elixir: `"""…"""` doc blocks are comments; ~r/…/ regexps and
// Regex.compile(…) ignored; ~s(…) / ~c(…) sigils are strings.
static int ElixirDocComment(Str s, int i) {
    return MatchBlock(s, i, "\"\"\"", "\"\"\"");
}
static int ElixirString(Str s, int i) {
    int n = MatchSingleLine(s, i, '\'');
    if (n > 0) {
        return n;
    }
    n = MatchSingleLine(s, i, '"');
    if (n > 0) {
        return n;
    }
    if ((LitLen(s, i, "~s(") > 0 || LitLen(s, i, "~c(") > 0)) {
        for (int at = i + 3; at < len(s) && s.s[at] != '\n'; at++) {
            if (s.s[at] == ')') {
                return at + 1 - i;
            }
        }
    }
    return -1;
}
static int ElixirRegexp(Str s, int i) {
    if (LitLen(s, i, "~r/") > 0) {
        for (int at = i + 3; at < len(s) && s.s[at] != '\n'; at++) {
            if (s.s[at] == '/') {
                return at + 1 - i;
            }
        }
        return -1;
    }
    return MatchCallWithString(s, i, "Regex.compile(", ElixirString);
}

void ScanElixir(Results* res, Str raw) {
    static const Alt kAlts[] = {
        {HashLineComment, "COMMENT"},
        {ElixirDocComment, "COMMENT"},
        {ElixirRegexp, nullptr},
        {ElixirString, "string"},
    };
    ScanAlts(res, raw, kAlts, 4);
}

// javascript (also typescript, jsx): strings and comments, plus the
// grammar's small HTML mode — text inside a matched <tag>…</tag> is
// corrected, the tags themselves and a `key:` string pass through.
static int JsString(Str s, int i) {
    char q = i < len(s) ? s.s[i] : 0;
    if (q == '\'') {
        return MatchMultiLine(s, i, '\'');
    }
    if (q == '"') {
        return MatchSingleLine(s, i, '"');
    }
    if (q == '`') {
        int n = MatchMultiLine(s, i, '`');
        if (n < 0) {
            return -1;
        }
        // "`"+ — extra closing backticks belong to the string.
        int at = i + n;
        while (at < len(s) && s.s[at] == '`') {
            at++;
        }
        return at - i;
    }
    return -1;
}

static int JsRegexp(Str s, int i) {
    int n = MatchSingleLine(s, i, '/');
    if (n > 0) {
        return n;
    }
    n = LitLen(s, i, "RegExp(");
    if (n > 0) {
        int at = i + n;
        while (at < len(s) && s.s[at] == ' ') {
            at++;
        }
        int sn = JsString(s, at);
        if (sn > 0) {
            at += sn;
            while (at < len(s) && s.s[at] != ')') {
                at++;
            }
            if (at < len(s)) {
                return at + 1 - i;
            }
        }
    }
    return -1;
}

// open_html `< … >` (the tag body may span lines); close_html `</ … >`.
static int JsOpenHtml(Str s, int i) {
    if (i >= len(s) || s.s[i] != '<') {
        return -1;
    }
    for (int at = i + 1; at < len(s); at++) {
        if (s.s[at] == '>') {
            return at + 1 - i;
        }
    }
    return -1;
}

static int JsCloseHtml(Str s, int i) {
    if (LitLen(s, i, "</") < 0) {
        return -1;
    }
    for (int at = i + 2; at < len(s); at++) {
        if (s.s[at] == '>') {
            return at + 1 - i;
        }
    }
    return -1;
}

// html_node = open_html ~ (!(close_html) ~ (html_node | text))+ ~ close_html
// — match only; the caller re-walks the span to emit.
static int JsHtmlNode(Str s, int i) {
    int n = JsOpenHtml(s, i);
    if (n < 0) {
        return -1;
    }
    int at = i + n;
    int children = 0;
    while (at < len(s)) {
        int c = JsCloseHtml(s, at);
        if (c > 0) {
            return children > 0 ? at + c - i : -1;
        }
        if (s.s[at] == '<') {
            int sub = JsHtmlNode(s, at);
            if (sub < 0) {
                return -1;
            }
            at += sub;
        } else {
            while (at < len(s) && s.s[at] != '<') {
                at++;
            }
        }
        children++;
    }
    return -1;
}

void ScanJavascript(Results* res, Str raw) {
    int ignoreStart = 0;
    int i = 0;
    auto flush = [&](int upTo) {
        if (upTo > ignoreStart) {
            EmitIgnore(res, Str(raw.s + ignoreStart, upTo - ignoreStart));
        }
    };
    while (i < len(raw)) {
        int n = CppLineComment(raw, i);
        if (n < 0) {
            n = CppBlockComment(raw, i);
        }
        if (n > 0) {
            flush(i);
            EmitText(res, StrL("COMMENT"), Str(raw.s + i, n));
            ignoreStart = i + n;
            i += n;
            continue;
        }
        // pair: a string key, ':', a string value — the key passes through.
        n = JsString(raw, i);
        if (n > 0) {
            int at = i + n;
            while (at < len(raw) && raw.s[at] == ' ') {
                at++;
            }
            if (at < len(raw) && raw.s[at] == ':') {
                at++;
                while (at < len(raw) && raw.s[at] == ' ') {
                    at++;
                }
                int vn = JsString(raw, at);
                if (vn > 0) {
                    flush(at);
                    EmitText(res, StrL("string"), Str(raw.s + at, vn));
                    ignoreStart = at + vn;
                    i = at + vn;
                    continue;
                }
            }
            flush(i);
            EmitText(res, StrL("string"), Str(raw.s + i, n));
            ignoreStart = i + n;
            i += n;
            continue;
        }
        n = JsRegexp(raw, i);
        if (n > 0) {
            i += n;
            continue;
        }
        if (raw.s[i] == '<') {
            int node = JsHtmlNode(raw, i);
            if (node > 0) {
                // Emit the node's span: tags pass through, text between
                // them is corrected — the grammar's `text` pairs.
                int at = i;
                int end = i + node;
                while (at < end) {
                    if (raw.s[at] == '<') {
                        int t = JsOpenHtml(raw, at);
                        at += t > 0 ? t : 1;
                        continue;
                    }
                    int textEnd = at;
                    while (textEnd < end && raw.s[textEnd] != '<') {
                        textEnd++;
                    }
                    flush(at);
                    EmitText(res, StrL("text"), Str(raw.s + at, textEnd - at));
                    ignoreStart = textEnd;
                    at = textEnd;
                }
                i = end;
                continue;
            }
            int tag = JsOpenHtml(raw, i);
            if (tag > 0) {
                // html_void — the tag alone, ignored.
                i += tag;
                continue;
            }
        }
        i++;
    }
    flush(len(raw));
}

// php: only what sits between <?php … ?> is source; the crate ignores
// everything outside.
static int PhpString(Str s, int i) {
    int n = MatchBlock(s, i, "\"\"\"", "\"\"\"");
    return n > 0 ? n : MatchBlock(s, i, "\"", "\"");
}
static int PhpRegexp(Str s, int i) {
    int n = MatchCallWithString(s, i, "preg_match_all(", PhpString);
    if (n > 0) {
        return n;
    }
    return MatchCallWithString(s, i, "preg_match(", PhpString);
}

void ScanPhp(Results* res, Str raw) {
    int ignoreStart = 0;
    int i = 0;
    bool inPhp = false;
    static const Alt kAlts[] = {
        {CppLineComment, "COMMENT"},  {HashLineComment, "COMMENT"},
        {CppBlockComment, "COMMENT"}, {PhpRegexp, nullptr},
        {PhpString, "string"},
    };
    while (i < len(raw)) {
        if (!inPhp) {
            // Comments are implicit at every level of the grammar, so they
            // are corrected outside <?php too.
            int matched = -1;
            const Alt* hit = nullptr;
            for (int k = 0; k < 3; k++) {
                matched = kAlts[k].match(raw, i);
                if (matched > 0) {
                    hit = &kAlts[k];
                    break;
                }
            }
            if (hit) {
                if (i > ignoreStart) {
                    EmitIgnore(res, Str(raw.s + ignoreStart, i - ignoreStart));
                }
                EmitText(res, Str(hit->rule), Str(raw.s + i, matched));
                ignoreStart = i + matched;
                i += matched;
                continue;
            }
            if (LitLen(raw, i, "<?php") > 0) {
                inPhp = true;
                i += 5;
                continue;
            }
            i++;
            continue;
        }
        if (LitLen(raw, i, "?>") > 0) {
            inPhp = false;
            i += 2;
            continue;
        }
        int matched = -1;
        const Alt* hit = nullptr;
        for (const Alt& alt : kAlts) {
            matched = alt.match(raw, i);
            if (matched > 0) {
                hit = &alt;
                break;
            }
        }
        if (!hit) {
            i++;
            continue;
        }
        if (hit->rule) {
            if (i > ignoreStart) {
                EmitIgnore(res, Str(raw.s + ignoreStart, i - ignoreStart));
            }
            EmitText(res, Str(hit->rule), Str(raw.s + i, matched));
            ignoreStart = i + matched;
        }
        i += matched;
    }
    if (len(raw) > ignoreStart) {
        EmitIgnore(res, Str(raw.s + ignoreStart, len(raw) - ignoreStart));
    }
}

// json: a `"…"` that a ':' follows is a key and passes through; every other
// string is a value and is corrected. `//` and `/*…*/` comments are in the
// crate's grammar too.
static int JsonString(Str s, int i) {
    // `"` ~ (chars | escape)* ~ `"` — this grammar honours \" escapes.
    if (i >= len(s) || s.s[i] != '"') {
        return -1;
    }
    for (int at = i + 1; at < len(s); at++) {
        if (s.s[at] == '\\') {
            at++;
            continue;
        }
        if (s.s[at] == '"') {
            return at + 1 - i;
        }
    }
    return -1;
}

void ScanJson(Results* res, Str raw) {
    int ignoreStart = 0;
    int i = 0;
    while (i < len(raw)) {
        int n = CppLineComment(raw, i);
        bool isComment = n > 0;
        if (!isComment) {
            n = CppBlockComment(raw, i);
            isComment = n > 0;
        }
        if (isComment) {
            if (i > ignoreStart) {
                EmitIgnore(res, Str(raw.s + ignoreStart, i - ignoreStart));
            }
            EmitText(res, StrL("COMMENT"), Str(raw.s + i, n));
            ignoreStart = i + n;
            i += n;
            continue;
        }
        n = JsonString(raw, i);
        if (n > 0) {
            // A key when the next non-space char is ':'.
            int at = i + n;
            while (at < len(raw) && (raw.s[at] == ' ' || raw.s[at] == '\t')) {
                at++;
            }
            bool isKey = at < len(raw) && raw.s[at] == ':';
            if (!isKey) {
                if (i > ignoreStart) {
                    EmitIgnore(res, Str(raw.s + ignoreStart, i - ignoreStart));
                }
                EmitText(res, StrL("string"), Str(raw.s + i, n));
                ignoreStart = i + n;
            }
            i += n;
            continue;
        }
        i++;
    }
    if (len(raw) > ignoreStart) {
        EmitIgnore(res, Str(raw.s + ignoreStart, len(raw) - ignoreStart));
    }
}

// yaml: `# comments` and the value half of `key: value` lines; everything
// else (list items, plain lines) passes through.
void ScanYaml(Results* res, Str raw) {
    int ignoreStart = 0;
    int i = 0;
    while (i < len(raw)) {
        int at = i;
        while (at < len(raw) && raw.s[at] == ' ') {
            at++;
        }
        if (at < len(raw) && raw.s[at] == '#') {
            int end = at;
            while (end < len(raw) && raw.s[end] != '\n') {
                end++;
            }
            if (at > ignoreStart) {
                EmitIgnore(res, Str(raw.s + ignoreStart, at - ignoreStart));
            }
            EmitText(res, StrL("comment"), Str(raw.s + at, end - at));
            ignoreStart = end;
            i = end < len(raw) ? end + 1 : end;
            continue;
        }
        // key: — chars that are not ':', quote or newline, then ':'.
        int keyEnd = at;
        if (at < len(raw) && raw.s[at] == '"') {
            int n = MatchSingleLine(raw, at, '"');
            keyEnd = n > 0 ? at + n : at;
        } else {
            while (keyEnd < len(raw) && raw.s[keyEnd] != ':' &&
                   raw.s[keyEnd] != '"' && raw.s[keyEnd] != '\'' &&
                   raw.s[keyEnd] != '\n') {
                keyEnd++;
            }
        }
        bool isPair = keyEnd > at && keyEnd < len(raw) && raw.s[keyEnd] == ':';
        if (!isPair) {
            // `other`: the rest of the line passes through.
            while (i < len(raw) && raw.s[i] != '\n') {
                i++;
            }
            if (i < len(raw)) {
                i++;
            }
            continue;
        }
        int valueStart = keyEnd + 1;
        if (valueStart < len(raw) && raw.s[valueStart] == ' ') {
            valueStart++;
        }
        // string = quoted (one line) or the rest of the line up to a quote.
        int valueEnd = valueStart;
        if (valueStart < len(raw) &&
            (raw.s[valueStart] == '"' || raw.s[valueStart] == '\'')) {
            int n = MatchSingleLine(raw, valueStart, raw.s[valueStart]);
            if (n > 0) {
                valueEnd = valueStart + n;
            }
        } else {
            while (valueEnd < len(raw) && raw.s[valueEnd] != '\n' &&
                   raw.s[valueEnd] != '"' && raw.s[valueEnd] != '\'') {
                valueEnd++;
            }
        }
        if (valueStart > ignoreStart) {
            EmitIgnore(res, Str(raw.s + ignoreStart, valueStart - ignoreStart));
        }
        EmitText(res, StrL("string"),
                 Str(raw.s + valueStart, valueEnd - valueStart));
        ignoreStart = valueEnd;
        i = valueEnd;
        while (i < len(raw) && raw.s[i] != '\n') {
            i++;
        }
        if (i < len(raw)) {
            i++;
        }
    }
    if (len(raw) > ignoreStart) {
        EmitIgnore(res, Str(raw.s + ignoreStart, len(raw) - ignoreStart));
    }
}

} // namespace autocorrect
