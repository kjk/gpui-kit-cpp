#include "base/text_boundary.h"
#include "base/grapheme_data.h"
#include "base/word_break_data.h"

namespace gpui {

template <typename Range, size_t N>
static const Range* BoundaryRangeAt(const Range (&ranges)[N], uint32_t code) {
    int first = 0;
    int last = (int)N;
    while (first < last) {
        int mid = first + (last - first) / 2;
        const auto& range = ranges[mid];
        if (code < range.first) {
            last = mid;
        } else if (code > range.last) {
            first = mid + 1;
        } else {
            return &range;
        }
    }
    return nullptr;
}

template <typename Kind, size_t N>
static Kind GraphemeRangeKind(const GraphemeRange<Kind> (&ranges)[N],
                              uint32_t code) {
    const auto* range = BoundaryRangeAt(ranges, code);
    return range ? range->kind : Kind::Other;
}

static GraphemeCategory GraphemeCategoryOf(uint32_t code) {
    using Cat = GraphemeCategory;
    if (code < 0x80) {
        if (code == '\r') return Cat::Cr;
        if (code == '\n') return Cat::Lf;
        return code < 0x20 || code == 0x7f ? Cat::Control : Cat::Other;
    }
    if (code >= 0xac00 && code <= 0xd7a3) {
        return (code - 0xac00) % 28 == 0 ? Cat::Lv : Cat::Lvt;
    }
    return GraphemeRangeKind(kGraphemeRanges, code);
}

static GraphemeCategory GraphemeCategoryAt(Str text, int offset) {
    uint32_t code = 0;
    Utf8At(text, offset, &code);
    return GraphemeCategoryOf(code);
}

static bool GraphemeControl(GraphemeCategory value) {
    using Cat = GraphemeCategory;
    return value == Cat::Control || value == Cat::Cr || value == Cat::Lf;
}

// GB9c looks back through an Indic linking sequence.
static bool GraphemeFollowsIndicLinker(Str text, int offset) {
    bool hasLinker = false;
    while (offset > 0) {
        offset = Utf8Prev(text, offset);
        uint32_t code = 0;
        Utf8At(text, offset, &code);
        IndicBreakCategory kind = GraphemeRangeKind(kIndicBreakRanges, code);
        if (kind == IndicBreakCategory::Linker) {
            hasLinker = true;
        } else if (kind != IndicBreakCategory::Extend) {
            return hasLinker &&
                   GraphemeCategoryOf(code) == GraphemeCategory::IndicConsonant;
        }
    }
    return false;
}

// GB11 looks left from the ZWJ for its pictograph.
static bool GraphemeZwjFollowsPictograph(Str text, int offset) {
    while (offset > 0) {
        offset = Utf8Prev(text, offset);
        GraphemeCategory kind = GraphemeCategoryAt(text, offset);
        if (kind != GraphemeCategory::Extend) {
            return kind == GraphemeCategory::ExtendedPictographic;
        }
    }
    return false;
}

static bool GraphemeOddRegionalPrefix(Str text, int offset) {
    bool odd = false;
    while (offset > 0) {
        offset = Utf8Prev(text, offset);
        if (GraphemeCategoryAt(text, offset) !=
            GraphemeCategory::RegionalIndicator) {
            break;
        }
        odd = !odd;
    }
    return odd;
}

// UAX #29 revision 47, extended grapheme clusters. Evaluate pair rules
// first and inspect earlier code points only for the contextual rules.
static bool GraphemeBreakAt(Str text, int offset) {
    if (offset <= 0 || offset >= len(text)) return true;
    using Cat = GraphemeCategory;
    int leftOffset = Utf8Prev(text, offset);
    Cat left = GraphemeCategoryAt(text, leftOffset);
    Cat right = GraphemeCategoryAt(text, offset);
    if (left == Cat::Cr && right == Cat::Lf) return false;
    if (GraphemeControl(left) || GraphemeControl(right)) return true;
    if (left == Cat::L && (right == Cat::L || right == Cat::V ||
                           right == Cat::Lv || right == Cat::Lvt))
        return false;
    if ((left == Cat::Lv || left == Cat::V) &&
        (right == Cat::V || right == Cat::T))
        return false;
    if ((left == Cat::Lvt || left == Cat::T) && right == Cat::T) return false;
    if (right == Cat::Extend || right == Cat::Zwj ||
        right == Cat::SpacingMark || left == Cat::Prepend)
        return false;
    if (right == Cat::IndicConsonant) {
        return !GraphemeFollowsIndicLinker(text, offset);
    }
    if (left == Cat::Zwj && right == Cat::ExtendedPictographic) {
        return !GraphemeZwjFollowsPictograph(text, leftOffset);
    }
    if (left == Cat::RegionalIndicator && right == Cat::RegionalIndicator) {
        return !GraphemeOddRegionalPrefix(text, offset);
    }
    return true;
}

bool TextIsGraphemeBoundary(Str text, int offset) {
    return Utf8IsCharBoundary(text, offset) && GraphemeBreakAt(text, offset);
}

int TextPreviousGraphemeBoundary(Str text, int offset) {
    offset = std::min(std::max(offset, 0), len(text));
    while (!Utf8IsCharBoundary(text, offset)) {
        offset++;
    }
    do {
        offset = Utf8Prev(text, offset);
    } while (!GraphemeBreakAt(text, offset));
    return offset;
}

int TextNextGraphemeBoundary(Str text, int offset) {
    offset = Utf8ClipLeft(text, offset);
    while (offset < len(text)) {
        uint32_t code = 0;
        offset += Utf8At(text, offset, &code);
        if (GraphemeBreakAt(text, offset)) break;
    }
    return offset;
}

enum class WordState : uint8_t {
    Stop,
    Other,
    Letter,
    Hebrew,
    Numeric,
    Katakana,
    ExtendNumLet,
    HalfFlag,
    Space,
    RequireLetter,
    RequireHebrew,
    RequireNumeric,
    AfterHebrewQuote,
};

static WordCategory WordCategoryOf(uint32_t code) {
    const auto* range = BoundaryRangeAt(kWordBreakRanges, code);
    return range ? range->kind : WordCategory::Other;
}

static bool WordCategoryIgnored(WordCategory word) {
    using Cat = WordCategory;
    return word == Cat::Extend || word == Cat::Format || word == Cat::Zwj;
}

static WordState WordStart(WordCategory word) {
    using Cat = WordCategory;
    using State = WordState;
    switch (word) {
        case Cat::Letter:
            return State::Letter;
        case Cat::Hebrew:
            return State::Hebrew;
        case Cat::Numeric:
            return State::Numeric;
        case Cat::Katakana:
            return State::Katakana;
        case Cat::ExtendNumLet:
            return State::ExtendNumLet;
        case Cat::Regional:
            return State::HalfFlag;
        case Cat::Space:
            return State::Space;
        default:
            return State::Other;
    }
}

static WordState WordContinue(WordState state, WordCategory previous,
                              WordCategory word) {
    using Cat = WordCategory;
    using State = WordState;
    switch (state) {
        case State::Letter:
        case State::Hebrew:
            if (state == State::Hebrew) {
                if (word == Cat::SingleQuote) return State::AfterHebrewQuote;
                if (word == Cat::DoubleQuote) return State::RequireHebrew;
            }
            if (word == Cat::MidLetter || word == Cat::MidNumLet ||
                word == Cat::SingleQuote)
                return State::RequireLetter;
            [[fallthrough]];
        case State::Numeric:
            if (state == State::Numeric &&
                (word == Cat::MidNum || word == Cat::MidNumLet ||
                 word == Cat::SingleQuote))
                return State::RequireNumeric;
            if (word == Cat::Letter || word == Cat::Hebrew ||
                word == Cat::Numeric || word == Cat::ExtendNumLet)
                return WordStart(word);
            break;
        case State::Katakana:
            if (word == Cat::Katakana || word == Cat::ExtendNumLet)
                return WordStart(word);
            break;
        case State::ExtendNumLet:
            if (word == Cat::Letter || word == Cat::Hebrew ||
                word == Cat::Numeric || word == Cat::Katakana ||
                word == Cat::ExtendNumLet)
                return WordStart(word);
            break;
        case State::HalfFlag:
            if (word == Cat::Regional) return State::Other;
            break;
        case State::Space:
            if (word == Cat::Space && previous == Cat::Space) return state;
            break;
        case State::RequireLetter:
        case State::AfterHebrewQuote:
            if (word == Cat::Letter || word == Cat::Hebrew)
                return WordStart(word);
            break;
        case State::RequireHebrew:
            if (word == Cat::Hebrew) return State::Hebrew;
            break;
        case State::RequireNumeric:
            if (word == Cat::Numeric) return State::Numeric;
            break;
        case State::Other:
        case State::Stop:
            break;
    }
    return State::Stop;
}

// Forward split_word_bounds from unicode-segmentation 1.13.3 (UAX #29).
// Tentative punctuation joins only if a matching letter or number follows.
int TextNextUnicodeWordBoundary(Str text, int offset) {
    using Cat = WordCategory;
    using State = WordState;
    offset = std::min(std::max(offset, 0), len(text));
    if (offset == len(text)) return offset;

    uint32_t code = 0;
    offset += Utf8At(text, offset, &code);
    Cat previous = WordCategoryOf(code);
    if (previous == Cat::Cr) {
        return offset < len(text) && text.s[offset] == '\n' ? offset + 1
                                                            : offset;
    }
    if (previous == Cat::Lf || previous == Cat::Newline) return offset;

    State state = WordStart(previous);
    int pending = -1;
    while (offset < len(text)) {
        int bytes = Utf8At(text, offset, &code);
        Cat word = WordCategoryOf(code);
        if (!WordCategoryIgnored(word)) {
            // WB3c checks the immediate predecessor before applying WB4.
            if (previous == Cat::Zwj &&
                BoundaryRangeAt(kWordPictographRanges, code)) {
                state = State::Other;
            } else {
                state = WordContinue(state, previous, word);
                if (state == State::Stop) break;
            }
            pending = state == State::RequireLetter ||
                              state == State::RequireHebrew ||
                              state == State::RequireNumeric
                          ? offset
                          : -1;
        }
        previous = word;
        offset += bytes;
    }
    return pending < 0 ? offset : pending;
}

// crates/base/src/text_boundary.rs. Two characters join into one word only
// when they are the same kind and that kind is Word or Whitespace, so a double
// click on a letter takes the word, one on a space takes the run of spaces,
// and one on punctuation or a CJK character takes just that character.

CharKind CharKindOf(uint32_t c) {
    bool word = c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                // Latin-1 Supplement through Latin Extended-B, the combining
                // marks, Cyrillic, and Latin Extended Additional: the ranges
                // CharacterKind::from spells out.
                (c >= 0x00C0 && c <= 0x024F) || (c >= 0x0300 && c <= 0x036F) ||
                (c >= 0x0400 && c <= 0x04FF) || (c >= 0x1E00 && c <= 0x1EFF);
    if (word) {
        return CharKind::Word;
    }
    if (c == '\n' || c == '\r') {
        return CharKind::Newline;
    }
    // Rust's char::is_whitespace (the newlines are answered above).
    return IsUnicodeWhitespace(c) ? CharKind::Whitespace : CharKind::Other;
}

// Unicode 17.0 simple lowercase mappings, from Rust char::to_lowercase.
struct LowercaseRange {
    uint32_t first;
    uint32_t last;
    int32_t delta;
    uint8_t step;
};

static const LowercaseRange kLowercaseRanges[] = {
    {0x00c0, 0x00d6, 32, 1},     {0x00d8, 0x00de, 32, 1},
    {0x0100, 0x012e, 1, 2},      {0x0132, 0x0136, 1, 2},
    {0x0139, 0x0147, 1, 2},      {0x014a, 0x0176, 1, 2},
    {0x0178, 0x0178, -121, 1},   {0x0179, 0x017d, 1, 2},
    {0x0181, 0x0181, 210, 1},    {0x0182, 0x0184, 1, 2},
    {0x0186, 0x0186, 206, 1},    {0x0187, 0x0187, 1, 1},
    {0x0189, 0x018a, 205, 1},    {0x018b, 0x018b, 1, 1},
    {0x018e, 0x018e, 79, 1},     {0x018f, 0x018f, 202, 1},
    {0x0190, 0x0190, 203, 1},    {0x0191, 0x0191, 1, 1},
    {0x0193, 0x0193, 205, 1},    {0x0194, 0x0194, 207, 1},
    {0x0196, 0x0196, 211, 1},    {0x0197, 0x0197, 209, 1},
    {0x0198, 0x0198, 1, 1},      {0x019c, 0x019c, 211, 1},
    {0x019d, 0x019d, 213, 1},    {0x019f, 0x019f, 214, 1},
    {0x01a0, 0x01a4, 1, 2},      {0x01a6, 0x01a6, 218, 1},
    {0x01a7, 0x01a7, 1, 1},      {0x01a9, 0x01a9, 218, 1},
    {0x01ac, 0x01ac, 1, 1},      {0x01ae, 0x01ae, 218, 1},
    {0x01af, 0x01af, 1, 1},      {0x01b1, 0x01b2, 217, 1},
    {0x01b3, 0x01b5, 1, 2},      {0x01b7, 0x01b7, 219, 1},
    {0x01b8, 0x01b8, 1, 1},      {0x01bc, 0x01bc, 1, 1},
    {0x01c4, 0x01c4, 2, 1},      {0x01c5, 0x01c5, 1, 1},
    {0x01c7, 0x01c7, 2, 1},      {0x01c8, 0x01c8, 1, 1},
    {0x01ca, 0x01ca, 2, 1},      {0x01cb, 0x01db, 1, 2},
    {0x01de, 0x01ee, 1, 2},      {0x01f1, 0x01f1, 2, 1},
    {0x01f2, 0x01f4, 1, 2},      {0x01f6, 0x01f6, -97, 1},
    {0x01f7, 0x01f7, -56, 1},    {0x01f8, 0x021e, 1, 2},
    {0x0220, 0x0220, -130, 1},   {0x0222, 0x0232, 1, 2},
    {0x023a, 0x023a, 10795, 1},  {0x023b, 0x023b, 1, 1},
    {0x023d, 0x023d, -163, 1},   {0x023e, 0x023e, 10792, 1},
    {0x0241, 0x0241, 1, 1},      {0x0243, 0x0243, -195, 1},
    {0x0244, 0x0244, 69, 1},     {0x0245, 0x0245, 71, 1},
    {0x0246, 0x024e, 1, 2},      {0x0370, 0x0372, 1, 2},
    {0x0376, 0x0376, 1, 1},      {0x037f, 0x037f, 116, 1},
    {0x0386, 0x0386, 38, 1},     {0x0388, 0x038a, 37, 1},
    {0x038c, 0x038c, 64, 1},     {0x038e, 0x038f, 63, 1},
    {0x0391, 0x03a1, 32, 1},     {0x03a3, 0x03ab, 32, 1},
    {0x03cf, 0x03cf, 8, 1},      {0x03d8, 0x03ee, 1, 2},
    {0x03f4, 0x03f4, -60, 1},    {0x03f7, 0x03f7, 1, 1},
    {0x03f9, 0x03f9, -7, 1},     {0x03fa, 0x03fa, 1, 1},
    {0x03fd, 0x03ff, -130, 1},   {0x0400, 0x040f, 80, 1},
    {0x0410, 0x042f, 32, 1},     {0x0460, 0x0480, 1, 2},
    {0x048a, 0x04be, 1, 2},      {0x04c0, 0x04c0, 15, 1},
    {0x04c1, 0x04cd, 1, 2},      {0x04d0, 0x052e, 1, 2},
    {0x0531, 0x0556, 48, 1},     {0x10a0, 0x10c5, 7264, 1},
    {0x10c7, 0x10c7, 7264, 1},   {0x10cd, 0x10cd, 7264, 1},
    {0x13a0, 0x13ef, 38864, 1},  {0x13f0, 0x13f5, 8, 1},
    {0x1c89, 0x1c89, 1, 1},      {0x1c90, 0x1cba, -3008, 1},
    {0x1cbd, 0x1cbf, -3008, 1},  {0x1e00, 0x1e94, 1, 2},
    {0x1e9e, 0x1e9e, -7615, 1},  {0x1ea0, 0x1efe, 1, 2},
    {0x1f08, 0x1f0f, -8, 1},     {0x1f18, 0x1f1d, -8, 1},
    {0x1f28, 0x1f2f, -8, 1},     {0x1f38, 0x1f3f, -8, 1},
    {0x1f48, 0x1f4d, -8, 1},     {0x1f59, 0x1f5f, -8, 2},
    {0x1f68, 0x1f6f, -8, 1},     {0x1f88, 0x1f8f, -8, 1},
    {0x1f98, 0x1f9f, -8, 1},     {0x1fa8, 0x1faf, -8, 1},
    {0x1fb8, 0x1fb9, -8, 1},     {0x1fba, 0x1fbb, -74, 1},
    {0x1fbc, 0x1fbc, -9, 1},     {0x1fc8, 0x1fcb, -86, 1},
    {0x1fcc, 0x1fcc, -9, 1},     {0x1fd8, 0x1fd9, -8, 1},
    {0x1fda, 0x1fdb, -100, 1},   {0x1fe8, 0x1fe9, -8, 1},
    {0x1fea, 0x1feb, -112, 1},   {0x1fec, 0x1fec, -7, 1},
    {0x1ff8, 0x1ff9, -128, 1},   {0x1ffa, 0x1ffb, -126, 1},
    {0x1ffc, 0x1ffc, -9, 1},     {0x2126, 0x2126, -7517, 1},
    {0x212a, 0x212a, -8383, 1},  {0x212b, 0x212b, -8262, 1},
    {0x2132, 0x2132, 28, 1},     {0x2160, 0x216f, 16, 1},
    {0x2183, 0x2183, 1, 1},      {0x24b6, 0x24cf, 26, 1},
    {0x2c00, 0x2c2f, 48, 1},     {0x2c60, 0x2c60, 1, 1},
    {0x2c62, 0x2c62, -10743, 1}, {0x2c63, 0x2c63, -3814, 1},
    {0x2c64, 0x2c64, -10727, 1}, {0x2c67, 0x2c6b, 1, 2},
    {0x2c6d, 0x2c6d, -10780, 1}, {0x2c6e, 0x2c6e, -10749, 1},
    {0x2c6f, 0x2c6f, -10783, 1}, {0x2c70, 0x2c70, -10782, 1},
    {0x2c72, 0x2c72, 1, 1},      {0x2c75, 0x2c75, 1, 1},
    {0x2c7e, 0x2c7f, -10815, 1}, {0x2c80, 0x2ce2, 1, 2},
    {0x2ceb, 0x2ced, 1, 2},      {0x2cf2, 0x2cf2, 1, 1},
    {0xa640, 0xa66c, 1, 2},      {0xa680, 0xa69a, 1, 2},
    {0xa722, 0xa72e, 1, 2},      {0xa732, 0xa76e, 1, 2},
    {0xa779, 0xa77b, 1, 2},      {0xa77d, 0xa77d, -35332, 1},
    {0xa77e, 0xa786, 1, 2},      {0xa78b, 0xa78b, 1, 1},
    {0xa78d, 0xa78d, -42280, 1}, {0xa790, 0xa792, 1, 2},
    {0xa796, 0xa7a8, 1, 2},      {0xa7aa, 0xa7aa, -42308, 1},
    {0xa7ab, 0xa7ab, -42319, 1}, {0xa7ac, 0xa7ac, -42315, 1},
    {0xa7ad, 0xa7ad, -42305, 1}, {0xa7ae, 0xa7ae, -42308, 1},
    {0xa7b0, 0xa7b0, -42258, 1}, {0xa7b1, 0xa7b1, -42282, 1},
    {0xa7b2, 0xa7b2, -42261, 1}, {0xa7b3, 0xa7b3, 928, 1},
    {0xa7b4, 0xa7c2, 1, 2},      {0xa7c4, 0xa7c4, -48, 1},
    {0xa7c5, 0xa7c5, -42307, 1}, {0xa7c6, 0xa7c6, -35384, 1},
    {0xa7c7, 0xa7c9, 1, 2},      {0xa7cb, 0xa7cb, -42343, 1},
    {0xa7cc, 0xa7da, 1, 2},      {0xa7dc, 0xa7dc, -42561, 1},
    {0xa7f5, 0xa7f5, 1, 1},      {0xff21, 0xff3a, 32, 1},
    {0x10400, 0x10427, 40, 1},   {0x104b0, 0x104d3, 40, 1},
    {0x10570, 0x1057a, 39, 1},   {0x1057c, 0x1058a, 39, 1},
    {0x1058c, 0x10592, 39, 1},   {0x10594, 0x10595, 39, 1},
    {0x10c80, 0x10cb2, 64, 1},   {0x10d50, 0x10d65, 32, 1},
    {0x118a0, 0x118bf, 32, 1},   {0x16e40, 0x16e5f, 32, 1},
    {0x16ea0, 0x16eb8, 27, 1},   {0x1e900, 0x1e921, 34, 1},
};

uint32_t CharLowerSimple(uint32_t c) {
    if (c < 0x80) {
        return c >= 'A' && c <= 'Z' ? c + 0x20 : c;
    }
    int lo = 0;
    int hi = dimofi(kLowercaseRanges);
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        const LowercaseRange& range = kLowercaseRanges[mid];
        if (c < range.first) {
            hi = mid;
        } else if (c > range.last) {
            lo = mid + 1;
        } else {
            return (c - range.first) % range.step == 0
                       ? (uint32_t)((int32_t)c + range.delta)
                       : c;
        }
    }
    return c;
}

// clip_offset_left: into the string, then back to a character boundary.
int Utf8ClipLeft(Str s, int off) {
    if (off > len(s)) {
        off = len(s);
    }
    if (off < 0) {
        off = 0;
    }
    while (off > 0 && off < len(s) && ((uint8_t)s.s[off] & 0xC0) == 0x80) {
        off--;
    }
    return off;
}

// Rust stops after 128 characters in each direction; a word longer than that
// is a wall of text, not something a double click should sweep up.
static const int kWordScanMax = 128;

bool TextWordRangeAt(Str s, int off, int* outA, int* outB) {
    if (!s.s || len(s) <= 0) {
        return false;
    }
    off = Utf8ClipLeft(s, off);
    if (off >= len(s)) {
        return false;
    }
    uint32_t c = 0;
    int clen = Utf8At(s, off, &c);
    CharKind kind = CharKindOf(c);
    bool joins = kind == CharKind::Word || kind == CharKind::Whitespace;
    int a = off;
    int b = off + clen;
    for (int i = 0; joins && a > 0 && i < kWordScanMax; i++) {
        int prev = Utf8Prev(s, a);
        uint32_t pc = 0;
        Utf8At(s, prev, &pc);
        if (CharKindOf(pc) != kind) {
            break;
        }
        a = prev;
    }
    for (int i = 0; joins && b < len(s) && i < kWordScanMax; i++) {
        uint32_t nc = 0;
        int nlen = Utf8At(s, b, &nc);
        if (CharKindOf(nc) != kind) {
            break;
        }
        b += nlen;
    }
    *outA = a;
    *outB = b;
    return true;
}

void TextLineRangeAt(Str s, int off, int* outA, int* outB) {
    *outA = 0;
    *outB = 0;
    if (!s.s || len(s) <= 0) {
        return;
    }
    off = Utf8ClipLeft(s, off);
    int a = 0;
    for (int i = off - 1; i >= 0; i--) {
        if (s.s[i] == '\n') {
            a = i + 1;
            break;
        }
    }
    int b = len(s);
    for (int i = off; i < len(s); i++) {
        if (s.s[i] == '\n') {
            b = i;
            break;
        }
    }
    *outA = a;
    *outB = b;
}

} // namespace gpui
