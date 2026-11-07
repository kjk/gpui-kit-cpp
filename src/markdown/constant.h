/* src/util/constant.rs — the sizes and lists the constructs are written
   against.

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md).
   The `gfm_tagfilter` and `sanitize_uri` lists are not here: they belong to
   `to_html`, which is not ported. */

#ifndef GPUI_MARKDOWN_CONSTANT_H_
#define GPUI_MARKDOWN_CONSTANT_H_

#include "base.h"

namespace markdown {

using base::SeqStrFirst;
using base::SeqStrings;
using base::SeqStrNext;
using base::Str;

// The maximum size of a scheme in an autolink: `mailto`.
constexpr int kAutolinkSchemeSizeMax = 32;
// The maximum size of a domain in a GFM autolink literal.
constexpr int kAutolinkDomainSizeMax = 63;
// `&#65533;` — the digits.
constexpr int kCharacterReferenceDecimalSizeMax = 7;
// `&#xfffd;` — the hex digits.
constexpr int kCharacterReferenceHexadecimalSizeMax = 6;
// `CounterClockwiseContourIntegral`, the longest name in the table below.
constexpr int kCharacterReferenceNamedSizeMax = 31;
// ```` ``` ````
constexpr int kCodeFencedSequenceSizeMin = 3;
// `---`
constexpr int kFrontmatterSequenceSize = 3;
// Two trailing spaces make a hard break.
constexpr int kHardBreakPrefixSizeMin = 2;
// `######`
constexpr int kHeadingAtxOpeningFenceSizeMax = 6;
// The characters after `<![`.
extern const Str kHtmlCdataPrefix;
// `textarea`
constexpr int kHtmlRawSizeMax = 8;
// The maximum size of a link reference label.
constexpr int kLinkReferenceSizeMax = 999;
// `4294967295.`
constexpr int kListItemValueSizeMax = 10;
// `$$`
constexpr int kMathFlowSequenceSizeMin = 2;
// How deep parens may nest in a raw resource destination.
constexpr int kResourceDestinationBalanceMax = 32;
// A tab stops every four columns.
constexpr int kTabSize = 4;
// `***`
constexpr int kThematicBreakMarkerCountMin = 3;

// HTML_BLOCK_NAMES: the tag names that open an HTML (flow) block of kind 6.
// HTML_RAW_NAMES: the ones that open a block of kind 1. Both are scanned end
// to end for a match, so both are `SeqStrings` runs rather than arrays of
// `Str` — the same bytes, without a pointer and a length per name.
extern const char kHtmlBlockNames[];
extern const char kHtmlRawNames[];

// CHARACTER_REFERENCES: name and value of every named character reference,
// sensitive to casing. It is the WHATWG table html5ever decodes with too, so
// the one copy is base's: `base::HtmlNamedCharRef`.

} // namespace markdown

#endif // GPUI_MARKDOWN_CONSTANT_H_
