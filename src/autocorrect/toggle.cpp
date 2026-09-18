/* config/toggle.rs — the `autocorrect-enable` / `autocorrect-disable` /
   `autocorrect: false` markers a comment can carry, with an optional rule
   list ("// autocorrect-disable space-word,fullwidth").

   Part of the C++ port of the `autocorrect` crate 2.14.2 (see
   src/autocorrect/readme.md). The crate parses these with a small pest
   grammar (config/toggle.pest); this is that grammar as a scan: find
   "autocorrect" anywhere in the comment, then match the marker shape. The
   crate keeps rule-name sets; the known names fit the mask, and hasUnknown
   remembers a name that is not one of ours, which is all the emptiness
   checks need — a merge only ever unions or clears a set. */

#include "autocorrect/internal.h"

namespace autocorrect {

static bool IsAsciiAlnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

// rule_name = (ASCII_ALPHANUMERIC ~ ("-" | "_")*)+ — returns the length
// matched at s[i], or 0.
static int MatchRuleName(Str s, int i) {
    int at = i;
    while (at < len(s) && IsAsciiAlnum(s.s[at])) {
        at++;
        while (at < len(s) && (s.s[at] == '-' || s.s[at] == '_')) {
            at++;
        }
    }
    return at - i;
}

static void ToggleAddRule(Toggle* t, Str name) {
    // Names are matched lowercased; RuleIdByName compares case-blind, so
    // the copy the crate lowercases is not needed here.
    int id = RuleIdByName(name);
    if (id >= 0) {
        t->mask = (uint16_t)(t->mask | (1u << id));
    } else {
        t->hasUnknown = true;
    }
}

Toggle ToggleParse(Str comment) {
    Str s = comment;
    static const Str kWord = StrL("autocorrect");
    for (int i = 0; i + len(kWord) <= len(s); i++) {
        if (!base::StrEq(Str(s.s + i, len(kWord)), kWord)) {
            continue;
        }
        int at = i + len(kWord);
        // (":" ~ " "*) | "-"
        if (at < len(s) && s.s[at] == ':') {
            at++;
            while (at < len(s) && s.s[at] == ' ') {
                at++;
            }
        } else if (at < len(s) && s.s[at] == '-') {
            at++;
        } else {
            continue;
        }
        ToggleKind kind;
        static const Str kEnable = StrL("enable");
        static const Str kTrue = StrL("true");
        static const Str kDisable = StrL("disable");
        static const Str kFalse = StrL("false");
        if (at + len(kEnable) <= len(s) &&
            base::StrEq(Str(s.s + at, len(kEnable)), kEnable)) {
            kind = ToggleKind::Enable;
            at += len(kEnable);
        } else if (at + len(kTrue) <= len(s) &&
                   base::StrEq(Str(s.s + at, len(kTrue)), kTrue)) {
            kind = ToggleKind::Enable;
            at += len(kTrue);
        } else if (at + len(kDisable) <= len(s) &&
                   base::StrEq(Str(s.s + at, len(kDisable)), kDisable)) {
            kind = ToggleKind::Disable;
            at += len(kDisable);
        } else if (at + len(kFalse) <= len(s) &&
                   base::StrEq(Str(s.s + at, len(kFalse)), kFalse)) {
            kind = ToggleKind::Disable;
            at += len(kFalse);
        } else {
            continue;
        }
        Toggle t;
        t.kind = kind;
        // pair* : " " ~ (rule_name ~ ","* ~ " "*)+
        while (at < len(s) && s.s[at] == ' ') {
            at++;
            for (;;) {
                int n = MatchRuleName(s, at);
                if (n <= 0) {
                    break;
                }
                ToggleAddRule(&t, Str(s.s + at, n));
                at += n;
                while (at < len(s) && (s.s[at] == ',' || s.s[at] == ' ')) {
                    at++;
                }
            }
        }
        return t;
    }
    Toggle none;
    none.kind = ToggleKind::None;
    return none;
}

void ToggleMerge(Toggle* t, Toggle neu) {
    if (neu.kind == ToggleKind::None) {
        *t = neu;
        return;
    }
    if (t->kind != neu.kind) {
        *t = neu;
        return;
    }
    // Same kind: union the sets, except an already-empty set (= "all rules")
    // stays empty, and merging an empty set clears — Toggle::merge.
    if (neu.RulesEmpty()) {
        t->mask = 0;
        t->hasUnknown = false;
        return;
    }
    if (t->RulesEmpty()) {
        return;
    }
    t->mask = (uint16_t)(t->mask | neu.mask);
    t->hasUnknown = t->hasUnknown || neu.hasUnknown;
}

uint16_t ToggleDisableRules(const Toggle* t) {
    return t->kind == ToggleKind::Disable ? t->mask : 0;
}

bool ToggleIsEnabled(const Toggle* t) {
    // Results::is_enabled — match_rule("") with a true default. A set that
    // names rules never contains "", so a named Enable answers false and a
    // named Disable answers true.
    switch (t->kind) {
        case ToggleKind::None:
            return true;
        case ToggleKind::Disable:
            return !t->RulesEmpty();
        case ToggleKind::Enable:
        default:
            return t->RulesEmpty();
    }
}

} // namespace autocorrect
