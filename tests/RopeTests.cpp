/* Ported from crates/base/src/input/base/rope_ext.rs, mod tests.
 *
 * Four of the Rust cases are about ropey itself rather than about RopeExt —
 * `test_lines`, `test_iter_lines` and `test_eq` exercise the iterator and the
 * comparison a `Rope` brings with it, and `test_replace` exercises
 * `Rope::replace`. The document here is a flat `Str`, so the first three have
 * nothing to test and the fourth is TextSplice in InputState.cpp, covered
 * through the edit path in InputStateTests.cpp.
 *
 * `word_range` / `word_at` are exercised through RopeExt below; the word
 * range a double click uses is text_boundary.rs's richer classifier, covered
 * separately by TextBoundaryTests.cpp. */

#include "Test.h"

// The rope every case but one is built from. Byte offsets, so the multi-byte
// runs are spelled out: "中文" is 6 bytes, "🎉" is 4.
static const char* kLines = "Hello\nWorld\r\nThis is a test 中文\nRope";
static const char* kMixed = "a 中文🎉 test\nRope";

static void SliceLine() {
    Str r = Str(kLines);
    utassert(base::StrEq(RopeSliceLine(r, 0), StrL("Hello")));
    // Lines split on LF alone, so the CR stays at the end of the line.
    utassert(base::StrEq(RopeSliceLine(r, 1), StrL("World\r")));
    utassert(base::StrEq(RopeSliceLine(r, 2), StrL("This is a test 中文")));
    utassert(base::StrEq(RopeSliceLine(r, 3), StrL("Rope")));
    // over bounds
    utassert(base::StrEq(RopeSliceLine(r, 6), StrL("")));

    // only have \r end
    Str cr = StrL("Hello\r");
    utassert(base::StrEq(RopeSliceLine(cr, 0), StrL("Hello\r")));
    utassert(base::StrEq(RopeSliceLine(cr, 1), StrL("")));
}

static void LinesLen() {
    utassert(RopeLinesLen(Str(kLines)) == 4);
    utassert(RopeLinesLen(Str{}) == 1);
    utassert(RopeLinesLen(StrL("Single line")) == 1);
    utassert(RopeLinesLen(StrL("Hello\r")) == 1);
}

static void LineStartEndOffset() {
    Str r = Str(kLines);
    utassert(RopeLineStartOffset(r, 0) == 0);
    utassert(RopeLineEndOffset(r, 0) == 5);

    utassert(RopeLineStartOffset(r, 1) == 6);
    utassert(RopeLineEndOffset(r, 1) == 12);

    utassert(RopeLineStartOffset(r, 2) == 13);
    utassert(RopeLineEndOffset(r, 2) == 34);

    utassert(RopeLineStartOffset(r, 3) == 35);
    utassert(RopeLineEndOffset(r, 3) == 39);

    utassert(RopeLineStartOffset(r, 4) == 39);
    utassert(RopeLineEndOffset(r, 4) == 39);
}

static void OffsetToPoint() {
    Str r = Str(kMixed);
    utassert(RopeOffsetToPoint(r, 0).row == 0);
    utassert(RopeOffsetToPoint(r, 0).column == 0);
    utassert(RopeOffsetToPoint(r, 1).column == 1);
    // "a 中" is 5 bytes
    utassert(RopeOffsetToPoint(r, 5).column == 5);
    // "a 中文🎉" is 12
    utassert(RopeOffsetToPoint(r, 12).column == 12);
    // "a 中文🎉 test\nR" is 19
    utassert(RopeOffsetToPoint(r, 19).row == 1);
    utassert(RopeOffsetToPoint(r, 19).column == 1);
}

static void PointToOffset() {
    Str r = Str(kMixed);
    utassert(RopePointToOffset(r, RopePoint{0, 0}) == 0);
    utassert(RopePointToOffset(r, RopePoint{0, 1}) == 1);
    utassert(RopePointToOffset(r, RopePoint{0, 5}) == 5);
    utassert(RopePointToOffset(r, RopePoint{0, 12}) == 12);
    utassert(RopePointToOffset(r, RopePoint{1, 1}) == 19);
}

static void CharAt() {
    Str r = StrL("Hello\nWorld\r\nThis is a test 中文🎉\nRope");
    uint32_t c = 0;
    utassert(RopeCharAt(r, 0, &c) && c == 'H');
    utassert(RopeCharAt(r, 5, &c) && c == '\n');
    utassert(RopeCharAt(r, 13, &c) && c == 'T');
    utassert(RopeCharAt(r, 28, &c) && c == 0x4E2D);  // 中
    utassert(RopeCharAt(r, 34, &c) && c == 0x1F389); // 🎉
    utassert(RopeCharAt(r, 38, &c) && c == '\n');
    // Past the end is Rust's None.
    utassert(RopeCharAt(r, 50, &c) == 0);
}

static void Utf16Conversion() {
    Str r = StrL("hello 中文🎉 test\nRope");
    utassert(RopeOffsetToOffsetUtf16(r, 5) == 5);
    utassert(RopeOffsetToOffsetUtf16(r, 9) == 7);  // "hello 中"
    utassert(RopeOffsetToOffsetUtf16(r, 12) == 8); // "hello 中文"
    utassert(RopeOffsetToOffsetUtf16(r, 16) == 10);
    utassert(RopeOffsetToOffsetUtf16(r, 100) == 20);

    utassert(RopeOffsetUtf16ToOffset(r, 5) == 5);
    utassert(RopeOffsetUtf16ToOffset(r, 7) == 9);
    utassert(RopeOffsetUtf16ToOffset(r, 8) == 12);
    utassert(RopeOffsetUtf16ToOffset(r, 10) == 16);
    utassert(RopeOffsetUtf16ToOffset(r, 100) == len(r));
}

static void ClipOffset() {
    Str r = StrL("Hello 中文🎉 test\nRope");
    // Inside '中' (3 bytes, 6..9)
    utassert(RopeClipOffset(r, 5, Bias::Left) == 5);
    utassert(RopeClipOffset(r, 7, Bias::Left) == 6);
    utassert(RopeClipOffset(r, 7, Bias::Right) == 9);
    utassert(RopeClipOffset(r, 9, Bias::Left) == 9);

    // Inside '🎉' (4 bytes, 12..16)
    utassert(RopeClipOffset(r, 13, Bias::Left) == 12);
    utassert(RopeClipOffset(r, 13, Bias::Right) == 16);
    utassert(RopeClipOffset(r, 16, Bias::Left) == 16);

    // At a character boundary
    utassert(RopeClipOffset(r, 5, Bias::Right) == 5);

    // Out of bounds
    utassert(RopeClipOffset(r, 26, Bias::Left) == 26);
    utassert(RopeClipOffset(r, 100, Bias::Left) == 26);
}

static void CharIndexToOffset() {
    Str r = Str(kMixed);
    utassert(RopeCharIndexToOffset(r, 0) == 0);
    utassert(RopeCharIndexToOffset(r, 1) == 1);
    utassert(RopeCharIndexToOffset(r, 3) == 5);  // "a 中"
    utassert(RopeCharIndexToOffset(r, 5) == 12); // "a 中文🎉"
    utassert(RopeCharIndexToOffset(r, 6) == 13); // "a 中文🎉 "

    utassert(RopeOffsetToCharIndex(r, 0) == 0);
    utassert(RopeOffsetToCharIndex(r, 1) == 1);
    utassert(RopeOffsetToCharIndex(r, 3) == 3);
    // Inside '中': clips right, so the whole character counts.
    utassert(RopeOffsetToCharIndex(r, 4) == 3);
    utassert(RopeOffsetToCharIndex(r, 5) == 3);
    utassert(RopeOffsetToCharIndex(r, 6) == 4);
    utassert(RopeOffsetToCharIndex(r, 10) == 5);
}

static void SourceRopeFacadesIterateAndDescribeEdits() {
    RopeExt rope = RopeExt::Of(Str(kLines));
    utassert(rope.LinesLen() == 4);
    utassert(base::StrEq(rope.SliceLines(1, 3),
                         StrL("World\r\nThis is a test 中文")));
    Selection word;
    utassert(rope.WordRange(7, &word));
    utassert(base::StrEq(rope.WordAt(7), StrL("World")));

    RopeLines lines = rope.IterLines();
    Str line;
    int count = 0;
    while (lines.Next(&line)) {
        count++;
    }
    utassert(count == 4 && lines.Len() == 0);

    InputEdit edit =
        InputEdit::New(StrL("one\ntwo"), {4, 7}, StrL("three\nfour"));
    utassert(edit.startByte == 4 && edit.oldEndByte == 7);
    utassert(edit.newEndByte == 14);
    utassert(edit.startPosition.row == 1 && edit.startPosition.column == 0);
    utassert(edit.oldEndPosition.row == 1 && edit.oldEndPosition.column == 3);
    utassert(edit.newEndPosition.row == 2 && edit.newEndPosition.column == 4);
}

static void TabSizeCountsAndBuildsIndent() {
    Arena* arena = ArenaNew();
    TabSize soft;
    soft.tabSize = 4;
    utassert(base::StrEq(soft.ToString(arena), StrL("    ")));
    utassert(soft.IndentCount(StrL("  \tabc")) == 6);
    utassert(soft.IndentCount(StrL("abc")) == 0);

    TabSize hard;
    hard.tabSize = 8;
    hard.hardTabs = true;
    utassert(base::StrEq(hard.ToString(arena), StrL("\t")));
    utassert(hard.IndentCount(StrL(" \t abc")) == 10);
    ArenaDelete(arena);
}

void TestRope() {
    TestSuite("rope_ext");
    SliceLine();
    LinesLen();
    LineStartEndOffset();
    OffsetToPoint();
    PointToOffset();
    CharAt();
    Utf16Conversion();
    ClipOffset();
    CharIndexToOffset();
    SourceRopeFacadesIterateAndDescribeEdits();
    TabSizeCountsAndBuildsIndent();
}
