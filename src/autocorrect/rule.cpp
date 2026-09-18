/* rule/mod.rs + rule/rule.rs — the rule chain. RULES run over each
   space-separated part of the text (a part that reads as a URL or a path is
   left alone), AFTER_RULES run over the joined result, and each rule applies
   only at its configured severity, exactly like Rule::format / Rule::lint.

   Part of the C++ port of the `autocorrect` crate 2.14.2 (see
   src/autocorrect/readme.md). Spellcheck is registered off by default in
   the crate's own config and is not ported; the readme says so. */

#include "autocorrect/internal.h"

namespace autocorrect {

// ─── PATH_RE / PATH_HASH_RE (the part skip) ──────────────────────────────

static bool IsAsciiAlnumCh(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

static bool IsPathCh(char c) {
    return IsAsciiAlnumCh(c) || c == '-' || c == '_' || c == '.';
}

// `(^[a-zA-Z\d]+://)|(^/?[a-zA-Z\d\-_\.]{2,}/)`
static bool IsMatchPath(Str s) {
    // scheme://
    int i = 0;
    while (i < len(s) && IsAsciiAlnumCh(s.s[i])) {
        i++;
    }
    if (i > 0 && i + 2 < len(s) && s.s[i] == ':' && s.s[i + 1] == '/' &&
        s.s[i + 2] == '/') {
        return true;
    }
    // /?name{2,}/
    i = 0;
    if (i < len(s) && s.s[i] == '/') {
        i++;
    }
    int start = i;
    while (i < len(s) && IsPathCh(s.s[i])) {
        i++;
    }
    return i - start >= 2 && i < len(s) && s.s[i] == '/';
}

// `[a-zA-Z0-9\-_.]+#[\w\-_.]*[\p{Han}]+[a-zA-Z0-9\-_.]*` — an anchor into a
// path, like `foo-bar_01.htm#测试copy`. Unanchored; checked on the trimmed
// part.
static bool IsWordDashDotCp(uint32_t cp) {
    return cp == '-' || cp == '_' || cp == '.' || IsWordCp(cp);
}

static bool IsMatchPathHash(Str s) {
    s = base::StrTrimAscii(s);
    for (int i = 0; i < len(s); i++) {
        if (!IsPathCh(s.s[i])) {
            continue;
        }
        int j = i;
        while (j < len(s) && IsPathCh(s.s[j])) {
            j++;
        }
        if (j >= len(s) || s.s[j] != '#') {
            i = j;
            continue;
        }
        // After '#': within the following run of word/dash/dot chars there
        // must be a Han char — `[\w\-_.]*` can absorb everything before it.
        int k = j + 1;
        while (k < len(s)) {
            int at = k;
            uint32_t cp = Utf8Next(s, &at);
            if (!IsWordDashDotCp(cp)) {
                break;
            }
            if (IsHan(cp)) {
                return true;
            }
            k = at;
        }
        i = j;
    }
    return false;
}

// ─── the rule tables ──────────────────────────────────────────────────────

using RuleFn = bool (*)(Arena*, Str, Str*);

struct RuleDef {
    int id;
    RuleFn fn;
};

// RULES, in registration order.
static const RuleDef kRules[] = {
    {kRuleSpaceWord, FormatSpaceWord},
    {kRuleSpacePunctuation, FormatSpacePunctuation},
    {kRuleSpaceBracket, FormatSpaceBracket},
    {kRuleSpaceDash, FormatSpaceDash},
    {kRuleSpaceBackticks, FormatSpaceBackticks},
    {kRuleSpaceDollar, FormatSpaceDollar},
    {kRuleFullwidth, FormatFullwidth},
};

// AFTER_RULES. Spellcheck would be the fifth; it is off by default and not
// ported.
static const RuleDef kAfterRules[] = {
    {kRuleHalfwidthWord, FormatHalfwidthWord},
    {kRuleHalfwidthPunctuation, FormatHalfwidthPunctuation},
    {kRuleNoSpaceFullwidth, FormatNoSpaceFullwidth},
    {kRuleNoSpaceFullwidthQuote, FormatNoSpaceFullwidthQuote},
};

// Rule::format / Rule::lint over one rule.
static void ApplyRule(Arena* a, const RuleDef& rule, bool lint,
                      uint16_t disableMask, RuleResult* result) {
    if (disableMask & (uint16_t)(1u << rule.id)) {
        return;
    }
    SeverityMode mode = RuleSeverity(rule.id);
    if (lint) {
        if (mode == SeverityMode::Off) {
            return;
        }
        Str neu;
        if (rule.fn(a, result->out, &neu)) {
            if (result->severity == Severity::Pass) {
                result->severity = mode == SeverityMode::Warning
                                       ? Severity::Warning
                                       : Severity::Error;
            }
            result->out = neu;
        }
        return;
    }
    if (mode != SeverityMode::Error) {
        return;
    }
    Str neu;
    if (rule.fn(a, result->out, &neu)) {
        result->severity = Severity::Error;
        result->out = neu;
    }
}

// format_part: RULES over one space-separated part, unless it is a path.
// (The crate then consults `textRules`; the default config has none.)
static void FormatPart(Arena* a, bool lint, uint16_t disableMask,
                       RuleResult* result) {
    if (IsMatchPath(result->out) || IsMatchPathHash(result->out)) {
        return;
    }
    for (const RuleDef& rule : kRules) {
        ApplyRule(a, rule, lint, disableMask, result);
    }
}

RuleResult FormatOrLintText(Arena* a, Str text, bool lint,
                            uint16_t disableMask) {
    RuleResult result;
    if (HasCjk(text)) {
        // Break at every space and newline, terminator included, and run
        // the part rules on each piece; severity threads through.
        StrBuilder joined;
        Severity severity = Severity::Pass;
        int start = 0;
        for (int i = 0; i < len(text); i++) {
            char c = text.s[i];
            if (c != ' ' && c != '\n' && c != '\r') {
                continue;
            }
            RuleResult sub;
            sub.out = Str(text.s + start, i + 1 - start);
            sub.severity = severity;
            FormatPart(a, lint, disableMask, &sub);
            joined.Append(sub.out);
            severity = sub.severity;
            start = i + 1;
        }
        if (start < len(text)) {
            RuleResult sub;
            sub.out = Str(text.s + start, len(text) - start);
            sub.severity = severity;
            FormatPart(a, lint, disableMask, &sub);
            joined.Append(sub.out);
            severity = sub.severity;
        }
        result.out = base::StrDup(a, Str(joined.els, joined.len));
        result.severity = severity;
    } else {
        result.out = text;
    }
    for (const RuleDef& rule : kAfterRules) {
        ApplyRule(a, rule, lint, disableMask, &result);
    }
    return result;
}

Str Format(Arena* a, Str text) {
    RuleResult r = FormatOrLintText(a, text, false, 0);
    // The caller owns everything; make sure even the pass-through case is
    // arena memory it can keep.
    return base::StrDup(a, r.out);
}

} // namespace autocorrect
