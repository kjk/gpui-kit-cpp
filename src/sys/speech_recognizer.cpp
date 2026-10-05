#include "sys/speech_recognizer.h"

namespace gpui {

// One character of `s` at `at`, and how many bytes it took.
static int SpeechCharAt(Str s, int at, uint32_t* out) {
    uint8_t b = (uint8_t)s.s[at];
    int n = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : 4;
    if (at + n > len(s)) {
        n = 1;
    }
    uint32_t c = b;
    if (n == 2) {
        c = b & 0x1F;
    } else if (n == 3) {
        c = b & 0x0F;
    } else if (n == 4) {
        c = b & 0x07;
    }
    for (int i = 1; i < n; i++) {
        c = (c << 6) | ((uint8_t)s.s[at + i] & 0x3F);
    }
    *out = c;
    return n;
}

static char AsciiLower(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

Str SpeechPhraseSeparator(Str tag) {
    int n = 0;
    while (n < len(tag) && tag.s[n] != '-') {
        n++;
    }
    static const char* const kUnspaced[] = {"zh", "yue", "ja", "th",
                                            "lo", "km",  "my"};
    for (const char* lang : kUnspaced) {
        int m = (int)strlen(lang);
        bool same = m == n;
        for (int i = 0; same && i < n; i++) {
            same = AsciiLower(tag.s[i]) == lang[i];
        }
        if (same) {
            return StrL("");
        }
    }
    return StrL(" ");
}

bool SpeechStartsOver(Str utterance, Str text) {
    int common = 0;
    int total = 0;
    bool matching = true;
    int b = 0;
    for (int a = 0; a < len(utterance);) {
        uint32_t ca = 0;
        a += SpeechCharAt(utterance, a, &ca);
        total++;
        if (matching && b < len(text)) {
            uint32_t cb = 0;
            b += SpeechCharAt(text, b, &cb);
            if (ca == cb) {
                common++;
                continue;
            }
        }
        matching = false;
    }
    return common * 2 < total;
}

// char::is_whitespace, for what a phrase can begin or end with.
static bool IsWhitespace(uint32_t c) {
    return c == ' ' || (c >= 0x09 && c <= 0x0D) || c == 0x85 || c == 0xA0 ||
           c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 ||
           c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

// Characters of scripts written without spaces between words: Chinese,
// Japanese and their punctuation.
static bool IsUnspacedScript(uint32_t c) {
    return (c >= 0x3000 && c <= 0x30FF) || (c >= 0x3400 && c <= 0x4DBF) ||
           (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) ||
           (c >= 0xFF00 && c <= 0xFFEF);
}

bool SpeechNeedsSpace(uint32_t before, uint32_t after) {
    bool closing = after == ',' || after == '.' || after == '?' ||
                   after == '!' || after == ';' || after == ':' || after == ')';
    return !IsWhitespace(before) && !IsWhitespace(after) && !closing &&
           !IsUnspacedScript(before) && !IsUnspacedScript(after);
}

} // namespace gpui
