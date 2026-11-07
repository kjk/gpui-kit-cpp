/* The crate's private modules, shared between this directory's files only:
   rule ids and default severities (config), the enable/disable Toggle,
   the codepoint classes the rules test, the rule engine (rule/mod.rs), and
   the Results emitter every grammar scanner feeds (code/code.rs).

   Part of the C++ port of the `autocorrect` crate 2.14.2 (see
   src/autocorrect/readme.md). */

#ifndef GPUI_AUTOCORRECT_INTERNAL_H_
#define GPUI_AUTOCORRECT_INTERNAL_H_

#include "autocorrect/autocorrect.h"

namespace autocorrect {

using base::ArenaVec;
using base::Str;
using base::StrBuilder;
using base::Vec;

// ─── config (config/mod.rs + .autocorrectrc.default) ─────────────────────

// The twelve rules, in the order rule/mod.rs registers them. The first seven
// are RULES (run per space-separated part), the rest AFTER_RULES (run over
// the whole text). Also the bit positions of a disable mask.
enum {
    kRuleSpaceWord = 0,
    kRuleSpacePunctuation,
    kRuleSpaceBracket,
    kRuleSpaceDash,
    kRuleSpaceBackticks,
    kRuleSpaceDollar,
    kRuleFullwidth,
    kRuleHalfwidthWord,
    kRuleHalfwidthPunctuation,
    kRuleNoSpaceFullwidth,
    kRuleNoSpaceFullwidthQuote,
    kRuleSpellcheck,
    kNRules,
};

// config/severity.rs SeverityMode; the values .autocorrectrc writes.
enum class SeverityMode : uint8_t {
    Off = 0,
    Error = 1,
    Warning = 2
};

// The default config's `rules:` map. There is no .autocorrectrc loading
// here — the editor runs the crate's built-in defaults, which the crate
// compiles in from .autocorrectrc.default.
SeverityMode RuleSeverity(int rule);
// Rule id by its kebab-case name ("space-word"), -1 when unknown. `name` is
// matched lowercased, the way toggle.rs lowercases what it parses.
int RuleIdByName(Str name);

// ─── toggle (config/toggle.rs) ────────────────────────────────────────────

// `// autocorrect: false` / `// autocorrect-disable space-word` state.
// The crate stores rule-name sets; the known names fit a bit mask, and one
// flag remembers that some unknown name was in the set, which is all
// `match_rule("")` (emptiness) needs.
enum class ToggleKind : uint8_t {
    None,
    Enable,
    Disable
};

struct Toggle {
    ToggleKind kind = ToggleKind::Enable;
    uint16_t mask = 0;
    bool hasUnknown = false;

    bool RulesEmpty() const { return mask == 0 && !hasUnknown; }
};

// toggle.rs parse(): reads an `autocorrect-…` marker out of a comment.
// Answers kind None when the comment has none.
Toggle ToggleParse(Str comment);
// Toggle::merge.
void ToggleMerge(Toggle* t, Toggle neu);
// Toggle::disable_rules — the mask the engine filters rules with.
uint16_t ToggleDisableRules(const Toggle* t);
// Results::is_enabled — match_rule("") defaulting to true.
bool ToggleIsEnabled(const Toggle* t);

// ─── codepoint classes (the regexes' \p{...}) ────────────────────────────

// Decode the codepoint at s[*i], advance *i past it. Invalid bytes decode as
// themselves, one byte at a time, so a scan always terminates.
uint32_t Utf8Next(Str s, int* i);
// Byte length of the UTF-8 sequence starting at s[i] (≥1).
int Utf8Len(Str s, int i);
// Decode without advancing.
uint32_t Utf8At(Str s, int i);
// How many codepoints s holds — Rust's chars().count().
int Utf8Count(Str s);

bool IsHan(uint32_t cp);
bool IsHangul(uint32_t cp);
bool IsKatakana(uint32_t cp);
bool IsHiragana(uint32_t cp);
bool IsBopomofo(uint32_t cp);
// \p{CJK}: any of the five. \p{CJ}: without Hangul.
bool IsCjk(uint32_t cp);
bool IsCj(uint32_t cp);
// The regex crate's \w (Unicode word character), approximated: ASCII
// alphanumerics and '_', the CJK scripts, fullwidth alphanumerics, and the
// common Latin/Greek/Cyrillic letter blocks. The readme records the
// approximation.
bool IsWordCp(uint32_t cp);

// ─── rule engine (rule/mod.rs, rule/rule.rs) ──────────────────────────────

// rule/rule.rs RuleResult. `out` is arena-allocated when a rule changed the
// text; otherwise it is the input slice.
struct RuleResult {
    Str out = {};
    Severity severity = Severity::Pass;
};

// rule/mod.rs format_or_lint_with_disable_rules: the whole rule chain over
// one piece of plain text. RULES run per space-separated part, AFTER_RULES
// over the joined result. `disableMask` is a toggle's disable_rules().
RuleResult FormatOrLintText(Arena* a, Str text, bool lint,
                            uint16_t disableMask);

// The individual rules (rule/word.rs, rule/fullwidth.rs, rule/halfwidth.rs).
// Each returns true and writes an arena string when it changed the text —
// Rust's Cow::Owned — and leaves *out alone otherwise.
bool FormatSpaceWord(Arena* a, Str in, Str* out);
bool FormatSpacePunctuation(Arena* a, Str in, Str* out);
bool FormatSpaceBracket(Arena* a, Str in, Str* out);
bool FormatSpaceDash(Arena* a, Str in, Str* out);
bool FormatSpaceBackticks(Arena* a, Str in, Str* out);
bool FormatSpaceDollar(Arena* a, Str in, Str* out);
bool FormatFullwidth(Arena* a, Str in, Str* out);
bool FormatHalfwidthWord(Arena* a, Str in, Str* out);
bool FormatHalfwidthPunctuation(Arena* a, Str in, Str* out);
bool FormatNoSpaceFullwidth(Arena* a, Str in, Str* out);
bool FormatNoSpaceFullwidthQuote(Arena* a, Str in, Str* out);

// rule/mod.rs CJK_RE.is_match.
bool HasCjk(Str s);

// ─── the Results emitter (code/code.rs) ───────────────────────────────────

// One object stands in for the crate's Results trait with its two
// implementations: a lint call collects LineResults, a format call rebuilds
// the document. The grammar scanners feed it regions in document order, and
// it keeps the (line, col) cursor pest would have kept — every byte of the
// input must pass through exactly one Emit call for the positions to hold.
struct Results {
    Arena* a = nullptr;
    bool lint = false;
    Toggle toggle = {};
    // The cursor: where the next region starts. 1-based; col in chars.
    int line = 1;
    int col = 1;
    Str error = {};
    // lint side
    ArenaVec<LineResult> lines = {};
    // format side (Vec's constructor is explicit, so no `= {}` here)
    StrBuilder out;
};

// Text the grammar does not correct — kept verbatim, cursor advanced.
void EmitIgnore(Results* res, Str part);
// A correctable region. `rule` is the grammar rule name; "comment" and
// "COMMENT" also parse the enable/disable toggle, exactly like
// code.rs format_or_lint.
void EmitText(Results* res, Str rule, Str part);

// What every scanner keeps between correctable regions: where the run of
// text it passes through verbatim began. `Flush` emits that run up to a
// byte; `Text` flushes it, emits the region raw[at, at+n) under `rule`, and
// starts the next run after it.
struct IgnoreRun {
    Results* res = nullptr;
    Str raw = {};
    int start = 0;

    void Flush(int upTo) {
        if (upTo > start) {
            EmitIgnore(res, Str(raw.s + start, upTo - start));
        }
    }
    void Text(Str rule, int at, int n) {
        Flush(at);
        EmitText(res, rule, Str(raw.s + at, n));
        start = at + n;
    }
};
// A fenced code block (markdown): lint/format the code as `lang` through
// LintFor/FormatFor, offset line numbers — code.rs
// format_or_lint_for_inline_scripts. `part` is the whole block including
// fences; `code` and `lang` point inside it.
void EmitCodeblock(Results* res, Str part, Str lang, Str code);
// The <style> / <script> bodies in HTML: same shape, fixed language.
void EmitInlineScript(Results* res, Str rule, Str part);
// A parse error: lint keeps what it has, format reverts to raw.
void EmitError(Results* res, Str raw, Str message);

// format_pair's Markdown hotfix: a block containing CJK disables
// halfwidth-punctuation for its children. Returns the toggle to restore.
Toggle ResultsPushCodeblockToggle(Results* res);

LintResult LintTake(Results* res, Str raw);
FormatResult FormatTake(Results* res, Str raw);

// ─── the grammar scanners (src/code/*.rs, grammar/*.pest) ────────────────

// Each walks the whole document and feeds `res` every byte. The readme maps
// them to the crate's grammars and lists the ones not ported.
void ScanMarkdown(Results* res, Str raw);
void ScanHtml(Results* res, Str raw);
void ScanRust(Results* res, Str raw);
void ScanJavascript(Results* res, Str raw);
void ScanC(Results* res, Str raw);
void ScanObjectiveC(Results* res, Str raw);
void ScanPython(Results* res, Str raw);
void ScanRuby(Results* res, Str raw);
void ScanGo(Results* res, Str raw);
void ScanSql(Results* res, Str raw);
void ScanCss(Results* res, Str raw);
void ScanConf(Results* res, Str raw);
void ScanJava(Results* res, Str raw);
void ScanCsharp(Results* res, Str raw);
void ScanSwift(Results* res, Str raw);
void ScanKotlin(Results* res, Str raw);
void ScanScala(Results* res, Str raw);
void ScanDart(Results* res, Str raw);
void ScanElixir(Results* res, Str raw);
void ScanPhp(Results* res, Str raw);
void ScanJson(Results* res, Str raw);
void ScanYaml(Results* res, Str raw);

} // namespace autocorrect
#endif // GPUI_AUTOCORRECT_INTERNAL_H_
