#include "base.h"
#include "unicode-linebreak/unicode_linebreak.h"
#include "unicode-linebreak/tables.h"

namespace unicode_linebreak {

BreakClass BreakProperty(uint32_t codepoint) {
    uint32_t dataPos;
    if (codepoint < 0x10000) {
        dataPos = kBreakPropTrieIndex[codepoint >> 6] + (codepoint & 63);
    } else if (codepoint < kBreakPropTrieHighStart) {
        uint32_t i1 = codepoint >> 14;
        uint32_t i2 =
            kBreakPropTrieIndex[i1 + 1024 - 4] + ((codepoint >> 9) & 31);
        uint32_t i3Block = kBreakPropTrieIndex[i2];
        uint32_t dataBlock =
            kBreakPropTrieIndex[i3Block + ((codepoint >> 4) & 31)];
        dataPos = dataBlock + (codepoint & 15);
    } else {
        return BreakClass::Unknown;
    }
    return (BreakClass)kBreakPropTrieData[dataPos];
}

static uint32_t Decode(Str text, int32_t* offset) {
    int32_t start = *offset;
    uint32_t value = 0;
    int n =
        Utf8StrictAt((const uint8_t*)text.s + start, len(text) - start, &value);
    if (n <= 0) {
        *offset = start + 1;
        return 0xFFFD;
    }
    *offset = start + n;
    return value;
}

LineBreakIterator LineBreaks(Str text) {
    LineBreakIterator iterator;
    iterator.text = text;
    return iterator;
}

bool LineBreakIterator::Next(LineBreak* result) {
    while (!finished) {
        int32_t at = offset;
        uint8_t cls;
        if (at < len(text)) {
            cls = (uint8_t)BreakProperty(Decode(text, &offset));
        } else {
            cls = 43; // eot
            finished = true;
        }
        uint8_t value = kPairTable[state][cls];
        bool mandatory = (value & 0x40) != 0;
        bool allowed = (value & 0x80) != 0 && (!afterZwj || mandatory);
        state = value & 0x3F;
        afterZwj = cls == (uint8_t)BreakClass::ZeroWidthJoiner;
        if (allowed) {
            *result = {at, mandatory ? BreakOpportunity::Mandatory
                                     : BreakOpportunity::Allowed};
            return true;
        }
    }
    return false;
}

SafeSplit SplitAtSafe(Str text) {
    int32_t at = len(text);
    BreakClass following = BreakClass::Unknown;
    bool haveFollowing = false;
    bool foundPair = false;
    while (at > 0) {
        int32_t start = at - 1;
        while (start > 0 && at - start < 4 &&
               ((uint8_t)text.s[start] & 0xC0) == 0x80) {
            start--;
        }
        int32_t decodedEnd = start;
        BreakClass cls = BreakProperty(Decode(text, &decodedEnd));
        if (decodedEnd != at) {
            start = at - 1;
            cls = BreakProperty(0xFFFD); // U+FFFD, consumed one byte.
        }
        if (foundPair) {
            return {Str(text.s, start), Str(text.s + start, len(text) - start)};
        }
        foundPair = haveFollowing && (kSafePairs[(uint8_t)cls] &
                                      (uint64_t(1) << (uint8_t)following)) != 0;
        following = cls;
        haveFollowing = true;
        at = start;
    }
    return {Str(text.s, 0), text};
}

} // namespace unicode_linebreak
