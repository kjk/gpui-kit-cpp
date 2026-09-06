/* Ported from crates/base/src/input/editor/search.rs, mod tests.
 *
 * All four Rust cases are here: they drive `SearchMatcher` directly and never
 * touch a window, so they come over whole. The one thing that had to change
 * is the shape of an assertion — Rust compares `matcher.next()` against an
 * `Option<Range>`, and here `next` answers whether there was one and writes
 * the range through a pointer. */

#include "Test.h"

static bool RangeIs(Selection s, int lo, int hi) {
    return s.start == lo && s.end == hi;
}

static bool LabelIs(const SearchMatcher* m, const char* want) {
    Arena* a = ArenaNew();
    Str got = SearchMatcherLabel(a, m);
    bool ok = base::StrEq(got, want);
    ArenaDelete(a);
    return ok;
}

// finds_navigates_and_preserves_replacement_position
static void FindsNavigatesAndKeepsItsPlaceThroughAReplacement() {
    SearchMatcher m;
    SearchMatcherUpdate(&m, StrL("foo FOO foo"));
    SearchMatcherUpdateQuery(&m, StrL("foo"), true);
    utassert(SearchMatcherLen(&m) == 3);
    utassert(RangeIs(m.ranges[0], 0, 3));
    utassert(RangeIs(m.ranges[1], 4, 7));
    utassert(RangeIs(m.ranges[2], 8, 11));

    Selection r = {};
    utassert(SearchMatcherNext(&m, &r) && RangeIs(r, 4, 7));
    utassert(SearchMatcherPrev(&m, &r) && RangeIs(r, 0, 3));

    // A replacement is announced before the text changes, so the cursor is
    // clamped into the shorter list instead of going back to the top.
    SearchMatcherSetIndex(&m, 2);
    SearchMatcherBeginReplacement(&m);
    SearchMatcherUpdate(&m, StrL("foo FOO bar"));
    utassert(SearchMatcherIndex(&m) == 1);
}

// next_wraps_to_start
static void NextWrapsToTheStart() {
    SearchMatcher m;
    SearchMatcherUpdate(&m, StrL(".....aaaaa.....aaaaa.....aaaaa"));
    SearchMatcherUpdateQuery(&m, StrL("aaaaa"), false);
    SearchMatcherSetIndex(&m, 2);
    Selection r = {};
    utassert(SearchMatcherNext(&m, &r) && RangeIs(r, 5, 10));
}

// replacement_keeps_current_match_index_on_next_match
static void AReplacementLeavesTheCursorOnWhatIsNowUnderIt() {
    SearchMatcher m;
    SearchMatcherUpdate(&m, StrL("foo foo foo"));
    SearchMatcherUpdateQuery(&m, StrL("foo"), true);
    utassert(LabelIs(&m, "1/3"));

    utassert(SearchMatcherHasNextWithoutWrap(&m));
    SearchMatcherBeginReplacement(&m);
    SearchMatcherUpdate(&m, StrL("bar foo foo"));
    utassert(SearchMatcherIndex(&m) == 0);
    utassert(RangeIs(m.ranges[0], 4, 7));
    utassert(LabelIs(&m, "1/2"));

    SearchMatcherSetIndex(&m, 1);
    utassert(!SearchMatcherHasNextWithoutWrap(&m));
    SearchMatcherSetIndex(&m, 0);
    SearchMatcherBeginReplacement(&m);
    SearchMatcherUpdate(&m, StrL("bar foo bar"));
    utassert(SearchMatcherIndex(&m) == 0);
    utassert(RangeIs(m.ranges[0], 4, 7));
    utassert(LabelIs(&m, "1/1"));
}

// update_matches_clamps_current_match_index_while_replacing
static void AReplacementClampsTheCursorIntoWhatIsLeft() {
    SearchMatcher m;
    SearchMatcherUpdate(&m, StrL("foo foo foo"));
    SearchMatcherUpdateQuery(&m, StrL("foo"), true);
    SearchMatcherSetIndex(&m, 2);
    SearchMatcherBeginReplacement(&m);

    SearchMatcherUpdate(&m, StrL("foo xoo foo"));

    utassert(SearchMatcherLen(&m) == 2);
    utassert(SearchMatcherIndex(&m) == 1);
    utassert(LabelIs(&m, "2/2"));
}

// The parts Rust's tests take for granted because aho-corasick is doing them.
// Here the scan is ours, so they are pinned.
static void TheScanItself() {
    SearchMatcher m;
    SearchMatcherUpdate(&m, StrL("Foo foo FOO fOo"));

    // ascii_case_insensitive(true) is a fold, not a locale.
    SearchMatcherUpdateQuery(&m, StrL("foo"), true);
    utassert(SearchMatcherLen(&m) == 4);
    SearchMatcherUpdateQuery(&m, StrL("foo"), false);
    utassert(SearchMatcherLen(&m) == 1);
    utassert(RangeIs(m.ranges[0], 4, 7));

    // An empty query is Rust's `None`: no automaton and no matches, rather
    // than a match at every offset.
    SearchMatcherUpdateQuery(&m, StrL(""), true);
    utassert(SearchMatcherIsEmpty(&m));
    utassert(LabelIs(&m, "0/0"));

    // Overlapping candidates come back leftmost and non-overlapping, which is
    // what `stream_find_iter` answers.
    SearchMatcherUpdate(&m, StrL("aaaa"));
    SearchMatcherUpdateQuery(&m, StrL("aa"), false);
    utassert(SearchMatcherLen(&m) == 2);
    utassert(RangeIs(m.ranges[0], 0, 2));
    utassert(RangeIs(m.ranges[1], 2, 4));

    // A query longer than the text matches nothing rather than reading past
    // the end of it.
    SearchMatcherUpdateQuery(&m, StrL("aaaaaaaa"), false);
    utassert(SearchMatcherIsEmpty(&m));

    // Neither end of the walk falls off: next wraps forward from the last and
    // prev wraps back from the first.
    SearchMatcherUpdate(&m, StrL("a.a"));
    SearchMatcherUpdateQuery(&m, StrL("a"), false);
    Selection r = {};
    utassert(SearchMatcherNext(&m, &r) && RangeIs(r, 2, 3));
    utassert(SearchMatcherNext(&m, &r) && RangeIs(r, 0, 1));
    utassert(SearchMatcherPrev(&m, &r) && RangeIs(r, 2, 3));

    // With nothing matched there is nowhere to go, and the walk says so
    // rather than answering a stale range.
    SearchMatcherUpdateQuery(&m, StrL("z"), false);
    utassert(!SearchMatcherNext(&m, &r));
    utassert(!SearchMatcherPrev(&m, &r));
    utassert(!SearchMatcherCurrent(&m, &r));
    utassert(!SearchMatcherPeek(&m, &r));
}

// update_cursor_by_offset: where a freshly opened panel starts from, which is
// the first match at or after what was on screen.
static void TheCursorStartsAtWhatWasOnScreen() {
    SearchMatcher m;
    SearchMatcherUpdate(&m, StrL("x..x..x..x"));
    SearchMatcherUpdateQuery(&m, StrL("x"), false);
    utassert(SearchMatcherLen(&m) == 4);

    SearchMatcherCursorByOffset(&m, 0);
    utassert(SearchMatcherIndex(&m) == 0);
    // Inside a match takes that match.
    SearchMatcherCursorByOffset(&m, 6);
    utassert(SearchMatcherIndex(&m) == 2);
    // Between two takes the one after.
    SearchMatcherCursorByOffset(&m, 5);
    utassert(SearchMatcherIndex(&m) == 2);
    // Past the last one there is nothing after, so it stops on the last —
    // Rust walks the whole list and leaves the index where it ended.
    SearchMatcherCursorByOffset(&m, 100);
    utassert(SearchMatcherIndex(&m) == 3);
}

// The unchanged text is an early return in Rust, and it ends a replacement on
// the way out — so a replacement that did not move a byte still ends.
static void TheSameTextIsNotRescanned() {
    SearchMatcher m;
    SearchMatcherUpdate(&m, StrL("foo foo foo"));
    SearchMatcherUpdateQuery(&m, StrL("foo"), true);
    SearchMatcherSetIndex(&m, 2);

    SearchMatcherUpdate(&m, StrL("foo foo foo"));
    utassert(SearchMatcherIndex(&m) == 2);
    utassert(SearchMatcherLen(&m) == 3);

    SearchMatcherBeginReplacement(&m);
    SearchMatcherUpdate(&m, StrL("foo foo foo"));
    utassert(!m.replacing);
    // And with the flag down, the next real change puts the cursor back to
    // the top rather than clamping it.
    SearchMatcherUpdate(&m, StrL("foo foo"));
    utassert(SearchMatcherIndex(&m) == 0);
}

// identical_query_keeps_the_current_match
static void IdenticalQueryKeepsTheCurrentMatch() {
    SearchSession session;
    SearchSessionSetQuery(&session, StrL("foo"), true);
    SearchMatcherUpdate(&session.matcher, StrL("foo bar foo baz foo"));
    SearchMatcherCursorByOffset(&session.matcher, 12);
    utassert(SearchMatcherIndex(&session.matcher) == 2);

    // Reopening Find and the styled search panel's initial query echo both
    // update the session with the same query. Neither should reset the
    // previously active occurrence.
    SearchSessionSetQuery(&session, StrL("foo"), true);

    utassert(SearchMatcherIndex(&session.matcher) == 2);
    utassert(LabelIs(&session.matcher, "3/3"));
}

void TestSearchMatcher() {
    FindsNavigatesAndKeepsItsPlaceThroughAReplacement();
    IdenticalQueryKeepsTheCurrentMatch();
    NextWrapsToTheStart();
    AReplacementLeavesTheCursorOnWhatIsNowUnderIt();
    AReplacementClampsTheCursorIntoWhatIsLeft();
    TheScanItself();
    TheCursorStartsAtWhatWasOnScreen();
    TheSameTextIsNotRescanned();
}
