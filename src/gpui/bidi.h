// UAX #9 paragraph direction: what the first strong character of a text
// decides. Inputs align by it; paint backends lay paragraphs out in it.
#ifndef GPUI_GPUI_BIDI_H_
#define GPUI_GPUI_BIDI_H_

#include "base.h"

namespace gpui {
using namespace base;

enum class ParagraphBidi : uint8_t {
    Left,
    Right,
    Break,
    Isolate,
    EndIsolate,
    Neutral
};

// Left, Right, or Neutral when no strong character precedes a paragraph break.
ParagraphBidi FirstStrongBidi(Str text);

} // namespace gpui
#endif // GPUI_GPUI_BIDI_H_
