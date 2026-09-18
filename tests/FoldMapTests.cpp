/* Ported from crates/base/src/input/editor/display_map/fold_map.rs.
 *
 * Rust has no test module of its own for the fold map, so these pin what
 * reading it says it does: the candidate list is sorted and one-per-start-
 * line, a closed fold hides the lines *between* its ends and not the ends
 * themselves, and an edit drops the folds it ran through and shifts the ones
 * below it. The last of those is the part a keystroke exercises constantly,
 * and the part that goes wrong quietly.
 *
 * The rows here are logical lines rather than Rust's wrap rows — see the
 * FoldMap comment in gpui.h — so a "display row" below is a line that is not
 * hidden. */

#include "Test.h"

static FoldRange R(int start, int end) {
    FoldRange r;
    r.startLine = start;
    r.endLine = end;
    return r;
}

// set_candidates sorts, and keeps the first range per start line.
static void CandidatesAreSortedAndOnePerStartLine() {
    FoldMap m;
    FoldRange in[] = {R(8, 12), R(2, 20), R(2, 5), R(0, 30)};
    FoldMapSetCandidates(&m, in, 4);

    utassert(m.candidates.len == 3);
    utassert(m.candidates[0].startLine == 0 && m.candidates[0].endLine == 30);
    // Of the two starting on line 2 the first one given wins, which is the
    // outermost node in Rust's walk.
    utassert(m.candidates[1].startLine == 2 && m.candidates[1].endLine == 20);
    utassert(m.candidates[2].startLine == 8 && m.candidates[2].endLine == 12);

    utassert(FoldMapIsCandidate(&m, 8));
    utassert(!FoldMapIsCandidate(&m, 9));
}

// A line that is not a candidate cannot be folded.
static void OnlyACandidateFolds() {
    FoldMap m;
    FoldRange in[] = {R(1, 4)};
    FoldMapSetCandidates(&m, in, 1);

    FoldMapToggle(&m, 2);
    utassert(!FoldMapIsFolded(&m, 2));

    FoldMapToggle(&m, 1);
    utassert(FoldMapIsFolded(&m, 1));
    FoldMapToggle(&m, 1);
    utassert(!FoldMapIsFolded(&m, 1));
}

// The projection with nothing folded is the identity, and it costs no
// storage: the fast path answers from the line count.
static void NoFoldsIsTheIdentity() {
    FoldMap m;
    FoldMapRebuild(&m, 6);

    utassert(FoldMapDisplayRowCount(&m) == 6);
    utassert(m.visibleLines.len == 0);
    for (int i = 0; i < 6; i++) {
        utassert(FoldMapDisplayRow(&m, i) == i);
        utassert(FoldMapLineAt(&m, i) == i);
        utassert(!FoldMapLineHidden(&m, i));
    }
    utassert(FoldMapDisplayRow(&m, 6) == -1);
    utassert(FoldMapLineAt(&m, 6) == -1);
}

// A closed fold hides the lines between its ends. Both ends stay on screen,
// so a folded block still reads as its opening line and its closing brace.
static void AClosedFoldHidesTheMiddle() {
    FoldMap m;
    FoldRange in[] = {R(1, 5)};
    FoldMapSetCandidates(&m, in, 1);
    FoldMapSetFolded(&m, 1, true);
    FoldMapRebuild(&m, 8);

    // 0 1 [2 3 4] 5 6 7 -> five rows.
    utassert(FoldMapDisplayRowCount(&m) == 5);
    utassert(!FoldMapLineHidden(&m, 1));
    utassert(FoldMapLineHidden(&m, 2));
    utassert(FoldMapLineHidden(&m, 4));
    utassert(!FoldMapLineHidden(&m, 5));

    utassert(FoldMapDisplayRow(&m, 5) == 2);
    utassert(FoldMapDisplayRow(&m, 7) == 4);
    utassert(FoldMapLineAt(&m, 2) == 5);

    // A hidden line reads as the line the fold starts on, which is where the
    // caret is drawn while its text is away.
    utassert(FoldMapNearestVisibleLine(&m, 3) == 1);
    utassert(FoldMapNearestVisibleLine(&m, 5) == 5);
}

// Nested folds, both closed: the hidden ranges merge rather than double-
// counting the lines they share.
static void NestedFoldsMerge() {
    FoldMap m;
    FoldRange in[] = {R(0, 9), R(2, 6)};
    FoldMapSetCandidates(&m, in, 2);
    FoldMapSetFolded(&m, 0, true);
    FoldMapSetFolded(&m, 2, true);
    FoldMapRebuild(&m, 10);

    // The outer fold already hides 1..8, so the inner one adds nothing.
    utassert(FoldMapDisplayRowCount(&m) == 2);
    utassert(FoldMapLineAt(&m, 0) == 0);
    utassert(FoldMapLineAt(&m, 1) == 9);
}

// Reopening restores every row, and clear_folds does it for all of them at
// once while leaving the candidates alone.
static void OpeningRestoresTheRows() {
    FoldMap m;
    FoldRange in[] = {R(1, 5)};
    FoldMapSetCandidates(&m, in, 1);
    FoldMapSetFolded(&m, 1, true);
    FoldMapRebuild(&m, 8);
    utassert(FoldMapDisplayRowCount(&m) == 5);

    FoldMapClearFolds(&m);
    FoldMapRebuild(&m, 8);
    utassert(FoldMapDisplayRowCount(&m) == 8);
    utassert(FoldMapIsCandidate(&m, 1));
}

// set_candidates forgets a fold whose candidate is no longer offered — the
// block it described is not in the document any more.
static void AFoldWithoutACandidateIsDropped() {
    FoldMap m;
    FoldRange in[] = {R(1, 5), R(7, 9)};
    FoldMapSetCandidates(&m, in, 2);
    FoldMapSetFolded(&m, 1, true);
    FoldMapSetFolded(&m, 7, true);

    FoldRange again[] = {R(7, 9)};
    FoldMapSetCandidates(&m, again, 1);
    utassert(!FoldMapIsFolded(&m, 1));
    utassert(FoldMapIsFolded(&m, 7));
}

// adjust_folds_for_edit: a range the edit ran through is dropped, and one
// below it moves by the lines the edit added or took away.
static void AnEditDropsWhatItRanThroughAndShiftsTheRest() {
    FoldMap m;
    FoldRange in[] = {R(1, 4), R(10, 20)};
    FoldMapSetCandidates(&m, in, 2);
    FoldMapSetFolded(&m, 10, true);

    // Two lines typed into line 2, which is inside the first range.
    FoldMapAdjustForEdit(&m, 2, 2, 2);
    utassert(!FoldMapIsCandidate(&m, 1));
    utassert(FoldMapIsCandidate(&m, 12));
    utassert(FoldMapIsFolded(&m, 12));
    utassert(m.candidates[0].endLine == 22);

    // And a deletion above it pulls it back.
    FoldMapAdjustForEdit(&m, 0, 1, -1);
    utassert(FoldMapIsCandidate(&m, 11));
    utassert(FoldMapIsFolded(&m, 11));
}

// An edit that touches nothing but the text of one line leaves the folds
// below it exactly where they were.
static void AnEditOnOneLineMovesNothing() {
    FoldMap m;
    FoldRange in[] = {R(4, 9)};
    FoldMapSetCandidates(&m, in, 1);
    FoldMapSetFolded(&m, 4, true);

    FoldMapAdjustForEdit(&m, 1, 1, 0);
    utassert(FoldMapIsCandidate(&m, 4));
    utassert(FoldMapIsFolded(&m, 4));
    utassert(m.candidates[0].endLine == 9);
}

// rebuild is skipped when neither the folds nor the line count moved, which
// is what keeps it off the keystroke path.
static void RebuildIsSkippedWhenNothingMoved() {
    FoldMap m;
    FoldRange in[] = {R(1, 5)};
    FoldMapSetCandidates(&m, in, 1);
    FoldMapSetFolded(&m, 1, true);
    FoldMapRebuild(&m, 8);
    utassert(!m.needsRebuild);

    FoldMapRebuild(&m, 8);
    utassert(FoldMapDisplayRowCount(&m) == 5);

    // A line count that changed is enough on its own.
    FoldMapRebuild(&m, 12);
    utassert(FoldMapDisplayRowCount(&m) == 9);
}

static void DisplayMapComposesWrappingAndFolding() {
    DisplayMap map(4);
    map.SetText(StrL("abcdef\ngh\nijklm"));
    utassert(map.BufferLineCount() == 3);
    utassert(map.WrapRowCount() == 5);
    utassert(map.DisplayRowCount() == 5);

    DisplayPoint display = map.BufferPosToDisplayPos(BufferPoint::New(0, 4));
    utassert(display.row == 1 && display.col == 0);
    BufferPoint buffer = map.DisplayPosToBufferPos(DisplayPoint::New(4, 1));
    utassert(buffer.line == 2 && buffer.col == 5);
    Selection lineRows = map.BufferLineToDisplayRowRange(2);
    utassert(lineRows.start == 3 && lineRows.end == 5);

    FoldRange fold = R(0, 2);
    map.SetFoldCandidates(&fold, 1);
    map.SetFolded(0, true);
    utassert(map.IsBufferLineHidden(1));
    utassert(map.DisplayRowCount() == 4);
    display = map.BufferPosToDisplayPos(BufferPoint::New(1, 1));
    utassert(display.row == 0 && display.col == 0);

    map.ClearFolds();
    utassert(map.DisplayRowCount() == 5);
}

static void DisplayMapWrapsUtf8AndReservesContinuationIndent() {
    DisplayMap utf8(2);
    utf8.SetText(StrL("中a"));
    // Two glyph columns, despite four UTF-8 bytes.
    utassert(utf8.WrapRowCount() == 1);
    utassert(utf8.DisplayRowCount() == 1);
    BufferPoint end = utf8.DisplayPosToBufferPos(DisplayPoint::New(0, 4));
    utassert(end.col == 4);

    DisplayMap indented(6);
    indented.SetText(StrL("  abcdefghij"));
    indented.SetWrappingIndent(WrappingIndent::None);
    utassert(indented.WrapRowCount() == 2);
    indented.SetWrappingIndent(WrappingIndent::Same);
    utassert(indented.WrapRowCount() == 3);

    TabSize tabs;
    tabs.tabSize = 4;
    indented.SetTabSize(tabs);
    indented.SetText(StrL("\tabcdef"));
    utassert(indented.WrapRowCount() == 3);
}

void TestFoldMap() {
    CandidatesAreSortedAndOnePerStartLine();
    OnlyACandidateFolds();
    NoFoldsIsTheIdentity();
    AClosedFoldHidesTheMiddle();
    NestedFoldsMerge();
    OpeningRestoresTheRows();
    AFoldWithoutACandidateIsDropped();
    AnEditDropsWhatItRanThroughAndShiftsTheRest();
    AnEditOnOneLineMovesNothing();
    RebuildIsSkippedWhenNothingMoved();
    DisplayMapComposesWrappingAndFolding();
    DisplayMapWrapsUtf8AndReservesContinuationIndent();
}
