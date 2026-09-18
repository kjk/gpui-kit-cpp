#include "base/input_rope.h"
#include "base/text_boundary.h"

namespace gpui {

InputEdit InputEdit::New(Str oldText, Selection range, Str inserted) {
    InputEdit edit;
    edit.startByte = RopeClipOffset(oldText, range.start, Bias::Left);
    edit.oldEndByte = RopeClipOffset(oldText, range.end, Bias::Right);
    edit.newEndByte = edit.startByte + len(inserted);
    edit.startPosition = RopeOffsetToPoint(oldText, edit.startByte);
    edit.oldEndPosition = RopeOffsetToPoint(oldText, edit.oldEndByte);
    edit.newEndPosition = edit.startPosition;
    for (int i = 0; i < len(inserted); i++) {
        if (inserted.s[i] == '\n') {
            edit.newEndPosition.row++;
            edit.newEndPosition.column = 0;
        } else {
            edit.newEndPosition.column++;
        }
    }
    return edit;
}

RopeLines RopeLines::New(Str rope) {
    return RopeLines{rope, 0, RopeLinesLen(rope)};
}

bool RopeLines::Next(Str* out) {
    if (row >= endRow) {
        return false;
    }
    if (out) {
        *out = RopeSliceLine(rope, row);
    }
    row++;
    return true;
}

int RopeExt::LineStartOffset(int row) const {
    return RopeLineStartOffset(text, row);
}

int RopeExt::LineEndOffset(int row) const {
    return RopeLineEndOffset(text, row);
}

Str RopeExt::SliceLine(int row) const {
    return RopeSliceLine(text, row);
}

Str RopeExt::SliceLines(int firstRow, int endRow) const {
    int rows = RopeLinesLen(text);
    int first = std::max(0, std::min(firstRow, rows));
    int end = std::max(first, std::min(endRow, rows));
    if (first == end) {
        return {};
    }
    int startOffset = RopeLineStartOffset(text, first);
    int endOffset = RopeLineEndOffset(text, end - 1);
    return Str(text.s + startOffset, endOffset - startOffset);
}

RopeLines RopeExt::IterLines() const {
    return RopeLines::New(text);
}

int RopeExt::LinesLen() const {
    return RopeLinesLen(text);
}

int RopeExt::LineLen(int row) const {
    return RopeLineLen(text, row);
}

int RopeExt::CharAt(int offset, uint32_t* out) const {
    return RopeCharAt(text, offset, out);
}

RopePoint RopeExt::OffsetToPoint(int offset) const {
    return RopeOffsetToPoint(text, offset);
}

int RopeExt::PointToOffset(RopePoint point) const {
    return RopePointToOffset(text, point);
}

int RopeExt::OffsetUtf16ToOffset(int offset) const {
    return RopeOffsetUtf16ToOffset(text, offset);
}

int RopeExt::OffsetToOffsetUtf16(int offset) const {
    return RopeOffsetToOffsetUtf16(text, offset);
}

int RopeExt::ClipOffset(int offset, Bias bias) const {
    return RopeClipOffset(text, offset, bias);
}

bool RopeExt::WordRange(int offset, Selection* out) const {
    int start = 0;
    int end = 0;
    if (!TextWordRangeAt(text, offset, &start, &end)) {
        return false;
    }
    if (out) {
        *out = {start, end};
    }
    return true;
}

Str RopeExt::WordAt(int offset) const {
    Selection range;
    if (!WordRange(offset, &range)) {
        return {};
    }
    return Str(text.s + range.start, range.end - range.start);
}

} // namespace gpui
