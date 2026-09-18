/* Ports crates/base/src/text_boundary.rs (word_range_at, line_range_at) and
   the word table in crates/ui/src/text/selection.rs — the boundaries a double
   click and a triple click select. */

#include "Test.h"

static void CheckWord(const char* text, int off, const char* want) {
    Str s = Str(text);
    int a = 0;
    int b = 0;
    bool got = TextWordRangeAt(s, off, &a, &b);
    utassert(got);
    if (!got) {
        return;
    }
    int wantLen = (int)strlen(want);
    utassert(b - a == wantLen);
    if (b - a == wantLen) {
        utassert(StrEq(Str(text + a, wantLen), Str(want, wantLen)));
    }
}

static void CheckLine(const char* text, int off, int wantA, int wantB) {
    int a = 0;
    int b = 0;
    TextLineRangeAt(Str(text), off, &a, &b);
    utassert(a == wantA);
    utassert(b == wantB);
}

void TestTextBoundary() {
    TestSuite("text_boundary");

    // ui/src/text/selection.rs, test_word_range_at: one string, every kind of
    // boundary in it. The offsets are byte offsets, as they are in Rust.
    const char* t =
        "test text\nabcde 中文🎉 test\nhello[()]\ntest_connector "
        "____\nRope\nrök\n"
        "grande île";
    CheckWord(t, 0, "test");
    CheckWord(t, 4, " ");
    CheckWord(t, 10, "abcde");
    CheckWord(t, 15, " ");
    // A CJK character is Other: it joins nothing, so a double click takes one.
    CheckWord(t, 16, "中");
    CheckWord(t, 19, "文");
    CheckWord(t, 22, "🎉");
    CheckWord(t, 27, "test");
    CheckWord(t, 37, "[");
    CheckWord(t, 38, "(");
    CheckWord(t, 39, ")");
    CheckWord(t, 40, "]");
    // '_' is a word character, so an identifier comes out whole — and a run
    // of underscores is a word of its own.
    CheckWord(t, 42, "test_connector");
    CheckWord(t, 56, " ");
    CheckWord(t, 57, "____");
    CheckWord(t, 62, "Rope");
    // Latin-1 and beyond: accented letters stay inside the word.
    CheckWord(t, 67, "rök");
    CheckWord(t, 79, "île");

    // text_selection.rs, double_click_expands_a_plain_run_to_the_input_word
    // _boundary: clicking inside "café" takes bytes 4..9, the é included.
    CheckWord("one café, three", 6, "café");
    // The comma is Other, so it is a word by itself.
    CheckWord("one café, three", 9, ",");

    // A run of spaces is one word, the way Whitespace connects to Whitespace.
    CheckWord("a   b", 2, "   ");

    // An offset inside a multi-byte character clips left to its start.
    CheckWord("café au lait", 4, "café");

    // Past the end there is no character, and so no word — Rust's
    // word_range_at returns None.
    int a = 0;
    int b = 0;
    utassert(!TextWordRangeAt(StrL("abc"), 3, &a, &b));
    utassert(!TextWordRangeAt(StrL(""), 0, &a, &b));

    // text_boundary.rs, line_range_at: the logical line, not the visual row.
    CheckLine("first line\nsecond line\nthird", 15, 11, 22);
    CheckLine("first line\nsecond line\nthird", 0, 0, 10);
    CheckLine("first line\nsecond line\nthird", 23, 23, 28);
    // No newline at all: the whole run, which is what a triple click on a
    // paragraph element gets.
    CheckLine("second line", 4, 0, 11);
}
