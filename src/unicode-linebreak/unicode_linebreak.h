/* unicode-linebreak 0.1.5, Unicode 15.0.0. Apache-2.0; see LICENSE. */
#pragma once

#include "base.h"

namespace unicode_linebreak {
using namespace base;

constexpr uint8_t kUnicodeVersion[3] = {15, 0, 0};

enum class BreakClass : uint8_t {
    Mandatory,
    CarriageReturn,
    LineFeed,
    CombiningMark,
    NextLine,
    Surrogate,
    WordJoiner,
    ZeroWidthSpace,
    NonBreakingGlue,
    Space,
    ZeroWidthJoiner,
    BeforeAndAfter,
    After,
    Before,
    Hyphen,
    Contingent,
    ClosePunctuation,
    CloseParenthesis,
    Exclamation,
    Inseparable,
    NonStarter,
    OpenPunctuation,
    Quotation,
    InfixSeparator,
    Numeric,
    Postfix,
    Prefix,
    Symbol,
    Ambiguous,
    Alphabetic,
    ConditionalJapaneseStarter,
    EmojiBase,
    EmojiModifier,
    HangulLvSyllable,
    HangulLvtSyllable,
    HebrewLetter,
    Ideographic,
    HangulLJamo,
    HangulVJamo,
    HangulTJamo,
    RegionalIndicator,
    ComplexContext,
    Unknown,
};

BreakClass BreakProperty(uint32_t codepoint);

enum class BreakOpportunity : uint8_t {
    Mandatory,
    Allowed
};
struct LineBreak {
    int32_t offset = 0; // UTF-8 byte offset of the character after the break.
    BreakOpportunity opportunity = BreakOpportunity::Allowed;
};

// Borrowed UTF-8, just like Rust's iterator. Copying preserves its position.
// Empty text emits no breaks; nonempty text ends with a mandatory break.
// The crate's SA -> AL tailoring is retained (no dictionary segmentation).
// Unlike Rust &str, Str can contain malformed UTF-8; consume each invalid
// byte as U+FFFD so callers still make progress.
struct LineBreakIterator {
    Str text;
    int32_t offset = 0;
    uint8_t state = 44; // sot
    bool afterZwj = false;
    bool finished = false;

    bool Next(LineBreak* result);
};

LineBreakIterator LineBreaks(Str text);

struct SafeSplit {
    Str previous;
    Str safe;
};
// Last split where subsequent breaks are independent of earlier context.
SafeSplit SplitAtSafe(Str text);

} // namespace unicode_linebreak
