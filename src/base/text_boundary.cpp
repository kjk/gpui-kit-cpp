#include "base/text_boundary.h"

namespace gpui {

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
    // Rust's char::is_whitespace, which is the Unicode White_Space property.
    bool space = c == ' ' || c == '\t' || c == 0x0B || c == 0x0C || c == 0x85 ||
                 c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
                 c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F ||
                 c == 0x3000;
    return space ? CharKind::Whitespace : CharKind::Other;
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
