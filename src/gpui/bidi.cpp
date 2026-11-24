#include "gpui/bidi.h"
#include "gpui/unicode_bidi_data.h"

namespace gpui {

static ParagraphBidi ParagraphBidiClass(uint32_t scalar) {
    int lo = 0, hi = dimofi(kParagraphBidiRanges);
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        const auto& range = kParagraphBidiRanges[mid];
        if (scalar < range.first)
            hi = mid;
        else if (scalar > range.last)
            lo = mid + 1;
        else
            return range.kind;
    }
    return ParagraphBidi::Left;
}

// UAX #9 P2/P3: content inside an isolate cannot choose the direction of
// its surrounding paragraph. Stop at a paragraph separator.
ParagraphBidi FirstStrongBidi(Str text) {
    int isolates = 0;
    for (int at = 0; at < len(text);) {
        uint32_t scalar;
        int bytes = Utf8At(text, at, &scalar);
        if (bytes <= 0) break;
        at += bytes;
        ParagraphBidi kind = ParagraphBidiClass(scalar);
        switch (kind) {
            case ParagraphBidi::Isolate:
                isolates++;
                break;
            case ParagraphBidi::EndIsolate:
                if (isolates > 0) isolates--;
                break;
            case ParagraphBidi::Break:
                return ParagraphBidi::Neutral;
            case ParagraphBidi::Left:
            case ParagraphBidi::Right:
                if (isolates == 0) return kind;
                break;
            case ParagraphBidi::Neutral:
                break;
        }
    }
    return ParagraphBidi::Neutral;
}

} // namespace gpui
