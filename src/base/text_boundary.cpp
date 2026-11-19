#include "base/text_boundary.h"
#include "base/grapheme_data.h"

namespace gpui {

template <typename Kind, size_t N>
static Kind GraphemeRangeKind(const GraphemeRange<Kind> (&ranges)[N],
                              uint32_t code) {
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
            return range.kind;
        }
    }
    return Kind::Other;
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

uint32_t CharLowerSimple(uint32_t c) {
    if (c >= 'A' && c <= 'Z') {
        return c + 0x20;
    }
    if ((c >= 0x00c0 && c <= 0x00d6) || (c >= 0x00d8 && c <= 0x00de)) {
        return c + 0x20;
    }
    if (c >= 0x0100 && c <= 0x012f && (c & 1) == 0) {
        return c + 1;
    }
    if (c >= 0x0132 && c <= 0x0137 && (c & 1) == 0) {
        return c + 1;
    }
    if (c >= 0x014a && c <= 0x0177 && (c & 1) == 0) {
        return c + 1;
    }
    if (c >= 0x0391 && c <= 0x03a1) {
        return c + 0x20;
    }
    if (c >= 0x03a3 && c <= 0x03ab) {
        return c + 0x20;
    }
    if (c >= 0x0410 && c <= 0x042f) {
        return c + 0x20;
    }
    if (c >= 0x0400 && c <= 0x040f) {
        return c + 0x50;
    }
    if (c >= 0x0531 && c <= 0x0556) {
        return c + 0x30;
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
