/* code/code.rs + code/mod.rs — what happens to the regions a grammar pulls
   out of a document: each is corrected line by line (format_or_lint), a
   fenced code block is re-dispatched as its own language
   (format_or_lint_for_inline_scripts), and everything else passes through
   untouched. LintFor / FormatFor at the bottom are the crate's lint_for /
   format_for dispatch.

   Part of the C++ port of the `autocorrect` crate 2.14.2 (see
   src/autocorrect/readme.md). The Results struct stands in for the crate's
   Results trait: pest gave the crate a (line, col) per pair, so here every
   byte of the input flows through exactly one Emit call and the cursor is
   advanced the way result/mod.rs line_col() counts. */

#include "autocorrect/internal.h"

#include <string.h>

namespace autocorrect {

// The default config's `context: codeblock: 1` — Markdown code blocks are
// corrected. (.autocorrectrc could turn this off; nothing here loads one.)
static const bool kContextCodeblockEnabled = true;

// result/mod.rs line_col(): advance the cursor over `part`. Columns count
// chars, the way pest counts them; \r\n is one newline, a lone \r is a char.
static void CursorAdvance(Results* res, Str part) {
    int i = 0;
    while (i < len(part)) {
        char c = part.s[i];
        if (c == '\r' && i + 1 < len(part) && part.s[i + 1] == '\n') {
            res->line++;
            res->col = 1;
            i += 2;
            continue;
        }
        if (c == '\n') {
            res->line++;
            res->col = 1;
            i++;
            continue;
        }
        res->col++;
        i += Utf8Len(part, i);
    }
}

void EmitIgnore(Results* res, Str part) {
    if (!res->lint) {
        res->out.Append(part);
    }
    CursorAdvance(res, part);
}

// str::trim, on the whitespace documents actually carry.
static Str TrimStart(Str s, int* leadingBytes) {
    int i = 0;
    while (i < len(s) && (s.s[i] == ' ' || s.s[i] == '\t' || s.s[i] == '\r' ||
                          s.s[i] == '\n' || s.s[i] == '\f')) {
        i++;
    }
    *leadingBytes = i;
    return Str(s.s + i, len(s) - i);
}

static Str TrimEnd(Str s) {
    int end = len(s);
    while (end > 0 && (s.s[end - 1] == ' ' || s.s[end - 1] == '\t' ||
                       s.s[end - 1] == '\r' || s.s[end - 1] == '\n' ||
                       s.s[end - 1] == '\f')) {
        end--;
    }
    return Str(s.s, end);
}

static Str Trim(Str s) {
    int leading = 0;
    return TrimEnd(TrimStart(s, &leading));
}

void EmitText(Results* res, Str rule, Str part) {
    int line0 = res->line;
    int col0 = res->col;
    // An enable/disable marker in a comment flips the toggle from here on —
    // including for this very comment, which is why the toggle is read
    // before is_enabled below.
    if (StrEq(rule, StrL("comment")) || StrEq(rule, StrL("COMMENT"))) {
        Toggle t = ToggleParse(part);
        if (t.kind != ToggleKind::None) {
            res->toggle = t;
        }
    }
    uint16_t mask = ToggleDisableRules(&res->toggle);
    bool enabled = ToggleIsEnabled(&res->toggle);
    if (res->lint) {
        if (!enabled) {
            CursorAdvance(res, part);
            return;
        }
        int subLine = 0;
        int lineStart = 0;
        for (int i = 0; i <= len(part); i++) {
            if (i < len(part) && part.s[i] != '\n') {
                continue;
            }
            Str lineStr(part.s + lineStart, i - lineStart);
            RuleResult lr = FormatOrLintText(res->a, lineStr, true, mask);
            if (lr.severity != Severity::Pass) {
                int leadingBytes = 0;
                Str trimmed = TrimEnd(TrimStart(lineStr, &leadingBytes));
                LineResult out;
                out.line = line0 + subLine;
                // The first line starts at the region's own column; a later
                // line starts after its leading whitespace, counted in
                // bytes — exactly the crate's arithmetic.
                out.col = subLine > 0 ? leadingBytes + 1 : col0;
                out.old = base::StrDup(res->a, trimmed);
                out.neu = base::StrDup(res->a, Trim(lr.out));
                out.severity = lr.severity;
                res->lines.Append(res->a, out);
            }
            subLine++;
            lineStart = i + 1;
        }
        CursorAdvance(res, part);
        return;
    }
    if (!enabled) {
        res->out.Append(part);
        CursorAdvance(res, part);
        return;
    }
    // format: each line through the rules, joined back with '\n'.
    int lineStart = 0;
    bool first = true;
    for (int i = 0; i <= len(part); i++) {
        if (i < len(part) && part.s[i] != '\n') {
            continue;
        }
        if (!first) {
            res->out.AppendChar('\n');
        }
        first = false;
        Str lineStr(part.s + lineStart, i - lineStart);
        RuleResult fr = FormatOrLintText(res->a, lineStr, false, mask);
        res->out.Append(fr.out);
        lineStart = i + 1;
    }
    CursorAdvance(res, part);
}

void EmitError(Results* res, Str raw, Str message) {
    (void)raw;
    res->error = base::StrDup(res->a, message);
}

// The shared half of codeblocks and inline scripts: lint/format `code` as
// `lang`, land the lines at this block's position.
static void EmitSub(Results* res, Str part, Str lang, Str code,
                    bool replaceInPart) {
    int baseLine = res->line;
    if (res->lint) {
        if (!ToggleIsEnabled(&res->toggle) || !kContextCodeblockEnabled) {
            CursorAdvance(res, part);
            return;
        }
        LintResult sub = LintFor(res->a, code, lang);
        if (sub.HasError()) {
            res->error = sub.error;
        }
        for (int i = 0; i < sub.nLines; i++) {
            LineResult line = sub.lines[i];
            // Inline script lines land at base_line - 1 below this block.
            line.line += baseLine - 1;
            res->lines.Append(res->a, line);
        }
        CursorAdvance(res, part);
        return;
    }
    if (!ToggleIsEnabled(&res->toggle) || !kContextCodeblockEnabled) {
        res->out.Append(part);
        CursorAdvance(res, part);
        return;
    }
    FormatResult sub = FormatFor(res->a, code, lang);
    if (sub.HasError()) {
        res->error = sub.error;
    }
    if (!replaceInPart) {
        res->out.Append(sub.out);
    } else if (len(code) == 0) {
        // An indented code block has no lang and no code child; formatting
        // the empty string leaves the block as it was.
        res->out.Append(part);
    } else {
        // Codeblock::update_data — the code replaced inside the fenced
        // block, fences kept.
        int at = 0;
        while (at + len(code) <= len(part)) {
            if (StrEq(Str(part.s + at, len(code)), code)) {
                res->out.Append(sub.out);
                at += len(code);
                continue;
            }
            res->out.AppendChar(part.s[at]);
            at++;
        }
        res->out.Append(Str(part.s + at, len(part) - at));
    }
    CursorAdvance(res, part);
}

void EmitCodeblock(Results* res, Str part, Str lang, Str code) {
    EmitSub(res, part, lang, code, true);
}

void EmitInlineScript(Results* res, Str rule, Str part) {
    Str lang = StrEq(rule, StrL("inline_style")) ? StrL("css") : StrL("js");
    EmitSub(res, part, lang, part, false);
}

Toggle ResultsPushCodeblockToggle(Results* res) {
    Toggle saved = res->toggle;
    Toggle disable;
    disable.kind = ToggleKind::Disable;
    disable.mask = (uint16_t)(1u << kRuleHalfwidthPunctuation);
    ToggleMerge(&res->toggle, disable);
    return saved;
}

LintResult LintTake(Results* res, Str raw) {
    (void)raw;
    LintResult out;
    out.lines = res->lines.Flatten(res->a);
    out.nLines = res->lines.len;
    out.error = res->error;
    return out;
}

FormatResult FormatTake(Results* res, Str raw) {
    FormatResult out;
    out.error = res->error;
    // FormatResult::error reverts to the raw input.
    out.out = len(res->error) > 0
                  ? base::StrDup(res->a, raw)
                  : base::StrDup(res->a, Str(res->out.els, res->out.len));
    return out;
}

// ─── lint_for / format_for dispatch (code/mod.rs) ────────────────────────

// The grammars with a scanner here. The crate also has latex, asciidoc,
// gettext, strings, xml and jupyter grammars; those are not ported (the
// readme says why), so their files answer the same empty result an unknown
// type gets.
static bool ScanForType(Str type, Results* res, Str raw) {
    struct Entry {
        const char* type;
        void (*scan)(Results*, Str);
    };
    static const Entry kTable[] = {
        {"html", ScanHtml},     {"yaml", ScanYaml},
        {"sql", ScanSql},       {"rust", ScanRust},
        {"ruby", ScanRuby},     {"elixir", ScanElixir},
        {"go", ScanGo},         {"javascript", ScanJavascript},
        {"css", ScanCss},       {"json", ScanJson},
        {"python", ScanPython}, {"objective_c", ScanObjectiveC},
        {"csharp", ScanCsharp}, {"swift", ScanSwift},
        {"java", ScanJava},     {"scala", ScanScala},
        {"kotlin", ScanKotlin}, {"php", ScanPhp},
        {"dart", ScanDart},     {"markdown", ScanMarkdown},
        {"conf", ScanConf},     {"c", ScanC},
        {"zig", ScanRust},      {"text", ScanMarkdown},
    };
    for (const Entry& e : kTable) {
        if (base::StrEq(type, Str(e.type))) {
            e.scan(res, raw);
            return true;
        }
    }
    return false;
}

LintResult LintFor(Arena* a, Str raw, Str filenameOrExt) {
    Str type = MatchFilename(a, filenameOrExt);
    Results res;
    res.a = a;
    res.lint = true;
    LintResult out;
    if (ScanForType(type, &res, raw)) {
        out = LintTake(&res, raw);
    }
    out.filepath = base::StrDup(a, filenameOrExt);
    return out;
}

FormatResult FormatFor(Arena* a, Str raw, Str filenameOrExt) {
    Str type = MatchFilename(a, filenameOrExt);
    Results res;
    res.a = a;
    res.lint = false;
    if (ScanForType(type, &res, raw)) {
        return FormatTake(&res, raw);
    }
    FormatResult out;
    out.out = base::StrDup(a, raw);
    return out;
}

} // namespace autocorrect
