/* The codepoint classes the crate reaches regex for: the five CJK script
   properties its `\p{CJK}` macro expands to, and an approximation of the
   regex crate's `\w`.

   Part of the C++ port of the `autocorrect` crate 2.14.2 (see
   src/autocorrect/readme.md). The ranges are the Unicode Script property
   assignments for Han, Hangul, Katakana, Hiragana and Bopomofo — the same
   sets `regex`'s \p{Han} etc. match. */

#include "autocorrect/internal.h"

namespace autocorrect {

uint32_t Utf8Next(Str s, int* i) {
    int at = *i;
    uint8_t b0 = (uint8_t)s.s[at];
    // An ASCII byte, an invalid lead, and a truncated tail all take one
    // byte as itself, so a scan always terminates.
    int size = 1;
    uint32_t cp = b0;
    if ((b0 & 0xE0) == 0xC0 && at + 1 < len(s)) {
        size = 2;
        cp = ((uint32_t)(b0 & 0x1F) << 6) | ((uint8_t)s.s[at + 1] & 0x3F);
    } else if ((b0 & 0xF0) == 0xE0 && at + 2 < len(s)) {
        size = 3;
        cp = ((uint32_t)(b0 & 0x0F) << 12) |
             (((uint32_t)(uint8_t)s.s[at + 1] & 0x3F) << 6) |
             ((uint8_t)s.s[at + 2] & 0x3F);
    } else if ((b0 & 0xF8) == 0xF0 && at + 3 < len(s)) {
        size = 4;
        cp = ((uint32_t)(b0 & 0x07) << 18) |
             (((uint32_t)(uint8_t)s.s[at + 1] & 0x3F) << 12) |
             (((uint32_t)(uint8_t)s.s[at + 2] & 0x3F) << 6) |
             ((uint8_t)s.s[at + 3] & 0x3F);
    }
    *i = at + size;
    return cp;
}

int Utf8Len(Str s, int i) {
    int at = i;
    Utf8Next(s, &at);
    return at - i;
}

uint32_t Utf8At(Str s, int i) {
    int at = i;
    return Utf8Next(s, &at);
}

int Utf8Count(Str s) {
    int n = 0;
    for (int i = 0; i < len(s);) {
        Utf8Next(s, &i);
        n++;
    }
    return n;
}

struct CpRange {
    uint32_t lo;
    uint32_t hi;
};

static bool InRanges(uint32_t cp, const CpRange* r, int n) {
    // The tables are sorted; a scan is fine for their size and the callers'.
    for (int i = 0; i < n; i++) {
        if (cp < r[i].lo) {
            return false;
        }
        if (cp <= r[i].hi) {
            return true;
        }
    }
    return false;
}

// Script=Han.
static const CpRange kHan[] = {
    {0x2E80, 0x2E99},   {0x2E9B, 0x2EF3},   {0x2F00, 0x2FD5},
    {0x3005, 0x3005},   {0x3007, 0x3007},   {0x3021, 0x3029},
    {0x3038, 0x303B},   {0x3400, 0x4DBF},   {0x4E00, 0x9FFF},
    {0xF900, 0xFA6D},   {0xFA70, 0xFAD9},   {0x20000, 0x2A6DF},
    {0x2A700, 0x2B739}, {0x2B740, 0x2B81D}, {0x2B820, 0x2CEA1},
    {0x2CEB0, 0x2EBE0}, {0x2EBF0, 0x2EE5D}, {0x2F800, 0x2FA1D},
    {0x30000, 0x3134A}, {0x31350, 0x323AF},
};

// Script=Hangul.
static const CpRange kHangul[] = {
    {0x1100, 0x11FF}, {0x302E, 0x302F}, {0x3131, 0x318E}, {0x3200, 0x321E},
    {0x3260, 0x327E}, {0xA960, 0xA97C}, {0xAC00, 0xD7A3}, {0xD7B0, 0xD7C6},
    {0xD7CB, 0xD7FB}, {0xFFA0, 0xFFBE}, {0xFFC2, 0xFFC7}, {0xFFCA, 0xFFCF},
    {0xFFD2, 0xFFD7}, {0xFFDA, 0xFFDC},
};

// Script=Katakana.
static const CpRange kKatakana[] = {
    {0x30A1, 0x30FA},   {0x30FD, 0x30FF},   {0x31F0, 0x31FF},
    {0x32D0, 0x32FE},   {0x3300, 0x3357},   {0xFF66, 0xFF6F},
    {0xFF71, 0xFF9D},   {0x1AFF0, 0x1AFF3}, {0x1AFF5, 0x1AFFB},
    {0x1AFFD, 0x1AFFE}, {0x1B000, 0x1B000}, {0x1B120, 0x1B122},
    {0x1B155, 0x1B155}, {0x1B164, 0x1B167},
};

// Script=Hiragana.
static const CpRange kHiragana[] = {
    {0x3041, 0x3096},   {0x309D, 0x309F},   {0x1B001, 0x1B11F},
    {0x1B132, 0x1B132}, {0x1B150, 0x1B152}, {0x1F200, 0x1F200},
};

// Script=Bopomofo.
static const CpRange kBopomofo[] = {
    {0x3105, 0x312F},
    {0x31A0, 0x31BF},
};

bool IsHan(uint32_t cp) {
    return cp >= 0x2E80 &&
           InRanges(cp, kHan, (int)(sizeof(kHan) / sizeof(kHan[0])));
}

bool IsHangul(uint32_t cp) {
    return cp >= 0x1100 &&
           InRanges(cp, kHangul, (int)(sizeof(kHangul) / sizeof(kHangul[0])));
}

bool IsKatakana(uint32_t cp) {
    return cp >= 0x30A1 &&
           InRanges(cp, kKatakana,
                    (int)(sizeof(kKatakana) / sizeof(kKatakana[0])));
}

bool IsHiragana(uint32_t cp) {
    return cp >= 0x3041 &&
           InRanges(cp, kHiragana,
                    (int)(sizeof(kHiragana) / sizeof(kHiragana[0])));
}

bool IsBopomofo(uint32_t cp) {
    return cp >= 0x3105 &&
           InRanges(cp, kBopomofo,
                    (int)(sizeof(kBopomofo) / sizeof(kBopomofo[0])));
}

bool IsCjk(uint32_t cp) {
    return IsHan(cp) || IsHangul(cp) || IsKatakana(cp) || IsHiragana(cp) ||
           IsBopomofo(cp);
}

bool IsCj(uint32_t cp) {
    return IsHan(cp) || IsKatakana(cp) || IsHiragana(cp) || IsBopomofo(cp);
}

bool IsWordCp(uint32_t cp) {
    if (cp < 0x80) {
        return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
               (cp >= '0' && cp <= '9') || cp == '_';
    }
    // The common letter blocks a document this linter sees actually uses:
    // Latin-1 letters (minus × ÷), Latin Extended, Greek, Cyrillic, the CJK
    // scripts, and the fullwidth alphanumerics halfwidth-word rewrites.
    if (cp >= 0xC0 && cp <= 0x24F) {
        return cp != 0xD7 && cp != 0xF7;
    }
    if (cp >= 0x370 && cp <= 0x4FF) {
        return true;
    }
    if ((cp >= 0xFF10 && cp <= 0xFF19) || (cp >= 0xFF21 && cp <= 0xFF3A) ||
        (cp >= 0xFF41 && cp <= 0xFF5A)) {
        return true;
    }
    return IsCjk(cp);
}

bool HasCjk(Str s) {
    for (int i = 0; i < len(s);) {
        if (IsCjk(Utf8Next(s, &i))) {
            return true;
        }
    }
    return false;
}

} // namespace autocorrect
