/* Ported from crates/base/src/input/editor/display_map/text_wrapper.rs and
 * the line_wrapper.rs it wraps with (gpui text_system).
 *
 * GPUI's wrap_line tests measure .ZedMono at 16px, whose every glyph is
 * 9.6px wide; the width function here answers that, so the boundaries are
 * the wrapper's own arithmetic and no font is involved. */

#include "Test.h"

#include <math.h>

namespace {

float MonoWidth(void*, uint32_t) {
    return 9.6f;
}

bool BoundariesAre(std::initializer_list<LineFragment> fragments, float width,
                   std::initializer_list<WrapBoundary> want) {
    Vec<WrapBoundary> got;
    LineWrapperWrapLine(fragments.begin(), (int)fragments.size(), width,
                        &MonoWidth, nullptr, &got);
    if (len(got) != (int)want.size()) {
        return false;
    }
    int i = 0;
    for (const WrapBoundary& b : want) {
        if (!(got[i++] == b)) {
            return false;
        }
    }
    return true;
}

bool TextBoundariesAre(const char* text, float width,
                       std::initializer_list<WrapBoundary> want) {
    return BoundariesAre({LineFragment::Text(Str(text))}, width, want);
}

} // namespace

// line_wrapper.rs test_wrap_line.
static void WrapLine() {
    utassert(TextBoundariesAre("aa bbb cccc ddddd eeee", 72,
                               {{7, 0}, {12, 0}, {18, 0}}));
    utassert(TextBoundariesAre("aaa aaaaaaaaaaaaaaaaaa", 72,
                               {{4, 0}, {11, 0}, {18, 0}}));
    utassert(TextBoundariesAre("     aaaaaaa", 72, {{7, 5}, {9, 5}, {11, 5}}));
    utassert(TextBoundariesAre("                            ", 72,
                               {{7, 0}, {14, 0}, {21, 0}}));
    utassert(TextBoundariesAre("          aaaaaaaaaaaaaa", 72,
                               {{7, 0}, {14, 3}, {18, 3}, {22, 3}}));

    // Several text fragments.
    utassert(BoundariesAre({LineFragment::Text(StrL("aa bbb ")),
                            LineFragment::Text(StrL("cccc ddddd eeee"))},
                           72, {{7, 0}, {12, 0}, {18, 0}}));
    // Text and elements mixed.
    utassert(BoundariesAre(
        {LineFragment::Text(StrL("aa ")), LineFragment::Element(20, 1),
         LineFragment::Text(StrL(" bbb ")), LineFragment::Element(30, 1),
         LineFragment::Text(StrL(" cccc"))},
        72, {{5, 0}, {9, 0}, {11, 0}}));
    // An element first, then text.
    utassert(BoundariesAre({LineFragment::Element(50, 1),
                            LineFragment::Text(StrL(" aaaa bbbb cccc dddd"))},
                           72, {{2, 0}, {7, 0}, {12, 0}, {17, 0}}));
    // An element wide enough to force a wrap on its own.
    utassert(BoundariesAre(
        {LineFragment::Text(StrL("short text ")), LineFragment::Element(100, 1),
         LineFragment::Text(StrL(" more text"))},
        72, {{6, 0}, {11, 0}, {12, 0}, {18, 0}}));
    // Non-breaking glue: 3, 2 and 3 bytes, so the boundary lands at 12.
    utassert(
        TextBoundariesAre("a\xE2\x80\xAF"
                          "b\xC2\xA0"
                          "c\xE2\x80\x91"
                          "d e",
                          72, {{12, 0}}));
}

namespace {

bool AllWordChars(const char* word) {
    Str s = Str(word);
    for (int i = 0; i < len(s);) {
        uint32_t c = 0;
        int n = Utf8At(s, i, &c);
        if (!LineWrapperIsWordChar(c)) {
            return false;
        }
        i += n > 0 ? n : 1;
    }
    return true;
}

} // namespace

// line_wrapper.rs test_is_word_char.
static void IsWordChar() {
    // Vietnamese (zed-industries/zed#23245), named so it is not one array
    // element glued out of two literals.
    const char* vietnamese =
        "ThậmchíđếnkhithuachạychúngcònnhẫntâmgiếtnốtsốđôngtùchínhtrịởYênBáivàCa"
        "oBằng";
    const char* words[] = {
        "Hello123", "non-English", "var_name", "123456", "3.1415", "10^2",
        "1~2", "100%", "@mention", "#hashtag", "$variable", "a=1",
        "Self::is_word_char", "on;", "more⋯", "won’t", "‘twas", "plz!", "see)",
        "quoted”", "well…", "github.com",
        // Latin-1 Supplement, Latin Extended-A and -B, Cyrillic.
        "ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏ", "ĀāĂăĄąĆćĈĉĊċČčĎď", "ƀƁƂƃƄƅƆƇƈƉƊƋƌƍƎƏ",
        "АБВГДЕЖЗИЙКЛМНОП", vietnamese,
        // Bengali.
        "গিয়েছিলেন", "ছেলে", "হচ্ছিল",
        // Non-breaking glue (UAX #14; zed-industries/zed#59664): NNBSP,
        // NBSP and NBH.
        "\xE2\x80\xAF", "\xC2\xA0", "\xE2\x80\x91"};
    for (const char* w : words) {
        utassert(AllWordChars(w));
    }
    const char* notWords[] = {"foo bar",
                              "zed-industries/zed",
                              "zed-industries\\zed",
                              "a=1&b=2",
                              "foo?b=2",
                              "你好",
                              "안녕하세요",
                              "こんにちは",
                              "😀😁😂",
                              "()[]{}<>"};
    for (const char* w : notWords) {
        utassert(!AllWordChars(w));
    }
}

namespace {

// text_wrapper.rs's fake_wrap_line: an indented line breaks once at 5 with
// an indent of 2, any other every 8 bytes.
void FakeWrapLine(void*, Str line, int, Vec<WrapBoundary>* out) {
    if (len(line) > 0 && line.s[0] == ' ') {
        VecAppend(*out, WrapBoundary{5, 2});
        return;
    }
    for (int i = 8; i < len(line); i += 8) {
        VecAppend(*out, WrapBoundary{i, 0});
    }
}

bool RowsAre(const Vec<int>& rows, std::initializer_list<int> want) {
    if (len(rows) != (int)want.size()) {
        return false;
    }
    int i = 0;
    for (int w : want) {
        if (rows[i++] != w) {
            return false;
        }
    }
    return true;
}

} // namespace

// text_wrapper.rs test_wrapping_indent_same_keeps_indent_reserved and
// test_wrapping_indent_none_continuation_lines_wrapped_at_full_width. The
// rows are the starts of `wrapped_lines`: [0..5, 5..24] is {0, 5}.
static void WrappingIndentSameAndNone() {
    Str text = StrL("  abcdefghijklmnopqrstuv");
    Vec<int> rows;
    int indent = -1;
    TextWrapperWrapItem(text, true, WrappingIndent::Same, &FakeWrapLine,
                        nullptr, &rows, &indent);
    utassert(indent == 2);
    utassert(RowsAre(rows, {0, 5}));

    TextWrapperWrapItem(text, true, WrappingIndent::None, &FakeWrapLine,
                        nullptr, &rows, &indent);
    utassert(indent == 0);
    utassert(RowsAre(rows, {0, 5, 13, 21}));

    // No wrap width: one row, however long the line.
    TextWrapperWrapItem(text, false, WrappingIndent::Same, &FakeWrapLine,
                        nullptr, &rows, &indent);
    utassert(RowsAre(rows, {0}) && indent == 0);
    // An empty line is still one row.
    TextWrapperWrapItem(Str{}, true, WrappingIndent::Same, &FakeWrapLine,
                        nullptr, &rows, &indent);
    utassert(RowsAre(rows, {0}));
}

// The rows a whole re-wrap of `s`'s text gives, against the rows `s` holds.
static bool WrapMatchesFreshWrap(const InputState& s, float width) {
    InputState fresh;
    fresh.kind = InputKind::Editor;
    fresh.softWrap = true;
    fresh.mode.tabSize = s.mode.tabSize;
    fresh.wrappingIndent = s.wrappingIndent;
    InputSetValue(&fresh, InputValue(&s));
    InputUpdateWrapMap(&fresh, nullptr, width, s.wrap.fontSize, s.wrap.fontWord);
    const InputWrapMap& a = s.wrap;
    const InputWrapMap& b = fresh.wrap;
    if (a.tree.LineCount() != b.tree.LineCount() || a.totalRows != b.totalRows)
        return false;
    for (int i = 0; i < a.tree.LineCount(); i++) {
        const InputWrapLine& x = *a.tree.Line(i);
        const InputWrapLine& y = *b.tree.Line(i);
        if (x.nRows != y.nRows || x.indent != y.indent ||
            a.tree.RowsAbove(i) != b.tree.RowsAbove(i))
            return false;
        for (int j = 0; j < x.nRows; j++) {
            if (x.starts[j] != y.starts[j]) return false;
        }
    }
    return true;
}

struct WrapEdit {
    int start;
    int end;
    const char* text;
};

// text_wrapper.rs `update`: an edit re-wraps the buffer rows it replaced and
// keeps every other row's wrap. Whatever the edit -- inside a line, across
// lines, adding or joining them, at either end, several before the rows are
// asked for -- the rows come out as a whole re-wrap of the new text would
// give them.
static void EditsRewrapTheLinesTheyTouched() {
    const float width = 120;
    InputState s;
    s.kind = InputKind::Editor;
    s.softWrap = true;
    InputSetValue(&s, StrL("fn main() {\n"
                           "    let one = alpha + beta + gamma + delta;\n"
                           "    short\n"
                           "\n"
                           "    let two = epsilon * zeta * eta * theta;\n"
                           "}\n"
                           "tail line with several words in it"));
    InputUpdateWrapMap(&s, nullptr, width, 14, 0);
    utassert(s.wrap.totalRows > s.wrap.tree.LineCount());
    // Each group is applied in full before the rows are brought up to date.
    const WrapEdit groups[][3] = {
        {{20, 20, "x"}, {-1, 0, nullptr}, {}},
        {{12, 12, "split\nhere and there and everywhere "}, {-1, 0, nullptr}},
        {{30, 70, ""}, {-1, 0, nullptr}},
        {{0, 0, "new first line, long enough to wrap twice over\n"},
         {-1, 0, nullptr}},
        {{5, 6, ""}, {40, 41, "\n\n"}, {2, 2, "abc def ghi jkl mno"}},
        {{60, 61, ""}, {-1, 0, nullptr}},
    };
    for (const auto& group : groups) {
        for (const WrapEdit& e : group) {
            if (!e.text) {
                break;
            }
            int n = len(InputValue(&s));
            Selection range = {e.start < n ? e.start : n,
                               e.end < n ? e.end : n};
            InputReplaceTextInRange(&s, nullptr, nullptr, &range, Str(e.text));
        }
        InputUpdateWrapMap(&s, nullptr, width, 14, 0);
        utassert(WrapMatchesFreshWrap(s, width));
    }
    // An edit at the very end, and one that empties the document.
    int n = len(InputValue(&s));
    Selection end = {n, n};
    InputReplaceTextInRange(&s, nullptr, nullptr, &end, StrL("\nmore"));
    InputUpdateWrapMap(&s, nullptr, width, 14, 0);
    utassert(WrapMatchesFreshWrap(s, width));
    Selection all = {0, len(InputValue(&s))};
    InputReplaceTextInRange(&s, nullptr, nullptr, &all, Str{});
    InputUpdateWrapMap(&s, nullptr, width, 14, 0);
    utassert(WrapMatchesFreshWrap(s, width));
    utassert(s.wrap.totalRows == 1);
}

namespace {

// One per grapheme: `s.graphemes(true).count()`.
float GraphemeCountWidth(void*, Str text) {
    float n = 0;
    for (int at = 0; at < len(text);) {
        uint32_t c = 0;
        int bytes = Utf8At(text, at, &c);
        at += bytes > 0 ? bytes : 1;
        if (TextIsGraphemeBoundary(text, at)) {
            n += 1;
        }
    }
    return n;
}

// ASCII is one wide and everything else two.
float AsciiOneElseTwoWidth(void*, Str text) {
    float w = 0;
    for (int at = 0; at < len(text);) {
        uint32_t c = 0;
        int bytes = Utf8At(text, at, &c);
        at += bytes > 0 ? bytes : 1;
        w += c < 0x80 ? 1.f : 2.f;
    }
    return w;
}

bool MeasuredIxsAre(const char* text, float width, WrappingIndent indent,
                    std::initializer_list<int> want) {
    Vec<WrapBoundary> got;
    MeasuredWrapBoundaries(Str(text), width, indent, &GraphemeCountWidth,
                           nullptr, &got);
    if (len(got) != (int)want.size()) {
        return false;
    }
    int i = 0;
    for (int ix : want) {
        if (got[i++].ix != ix) {
            return false;
        }
    }
    return true;
}

void MeasuredCjkLatinWrapLine(void*, Str line, int, Vec<WrapBoundary>* out) {
    MeasuredWrapBoundaries(line, 6, WrappingIndent::None, &AsciiOneElseTwoWidth,
                           nullptr, out);
}

} // namespace

// measured_wrap_keeps_cjk_latin_boundary_stable_during_edits: "abcd" and
// U+7684, then Latin letters typed and deleted after it. Rust drives the
// TextWrapper's incremental update; the row builder here is the same item
// builder the wrap map calls for each line it re-wraps.
static void MeasuredWrapKeepsCjkLatinBoundaryStableDuringEdits() {
    const char* values[] = {"abcd\xE7\x9A\x84", "abcd\xE7\x9A\x84s",
                            "abcd\xE7\x9A\x84ss", "abcd\xE7\x9A\x84s",
                            "abcd\xE7\x9A\x84"};
    for (const char* value : values) {
        Str text = Str(value);
        Vec<int> rows;
        int indent = 0;
        TextWrapperWrapItem(text, true, WrappingIndent::None,
                            &MeasuredCjkLatinWrapLine, nullptr, &rows, &indent);
        bool latin = text.s[len(text) - 1] == 's';
        if (latin) {
            // The row with the already-fitting Chinese character keeps it.
            utassert(RowsAre(rows, {0, 7}));
        } else {
            utassert(RowsAre(rows, {0}));
        }
    }
}

// measured_wrap_preserves_words_graphemes_and_indentation
static void MeasuredWrapPreservesWordsGraphemesAndIndentation() {
    utassert(MeasuredIxsAre("hello world", 8, WrappingIndent::None, {6}));
    // Hebrew letters must not break after a hyphen (LB21a).
    utassert(MeasuredIxsAre("\xD7\x90-bc def", 4, WrappingIndent::None, {5}));
    // Numeric prefixes/postfixes stay with the number (LB25).
    utassert(MeasuredIxsAre("a $12% b", 5, WrappingIndent::None, {2, 7}));
    // "a", the woman-technologist ZWJ sequence, "b".
    const char* emoji =
        "a\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB"
        "b";
    utassert(MeasuredIxsAre(emoji, 2, WrappingIndent::None, {12}));
    utassert(MeasuredIxsAre("  abcdefgh", 5, WrappingIndent::Same, {5, 8}));
    utassert(MeasuredIxsAre("  abcdefgh", 5, WrappingIndent::None, {2, 7}));
    utassert(MeasuredIxsAre("", 0, WrappingIndent::None, {}));
    utassert(MeasuredIxsAre("abc", 0, WrappingIndent::None, {1, 2}));
    // Closing punctuation stays with the preceding Chinese character:
    // U+4F60 U+597D U+FF0C U+4E16 U+754C.
    utassert(MeasuredIxsAre(
        "\xE4\xBD\xA0\xE5\xA5\xBD\xEF\xBC\x8C\xE4\xB8\x96\xE7\x95\x8C", 2,
        WrappingIndent::None, {3, 9}));
}

static void WrapTreeKeepsSuffixStorageAndSummaries() {
    InputWrapTree tree;
    int starts[] = {0, 4, 9};
    for (int i = 0; i < 10000; i++) tree.Insert(i, starts, 1 + i % 3, 0);
    const int* suffix = tree.Line(9999)->starts;
    int originalRows = tree.RowCount();
    utassert(tree.Height() < 20);
    tree.Remove(0, 1);
    tree.Insert(0, starts, 3, 0);
    utassert(tree.Line(9999)->starts == suffix);
    utassert(tree.RowCount() == originalRows + 2);
    int rows = 0;
    for (int i = 0; i < tree.LineCount(); i++) {
        utassert(tree.RowsAbove(i) == rows);
        for (int j = 0; j < tree.Line(i)->nRows; j++)
            utassert(tree.LineAtRow(rows + j) == i);
        rows += tree.Line(i)->nRows;
    }
    tree.Remove(123, 7000);
    utassert(tree.LineCount() == 3000);
    utassert(tree.Height() < 17);
    utassert(tree.Line(2999)->starts == suffix);
    rows = 0;
    for (int i = 0; i < tree.LineCount(); i++) {
        utassert(tree.RowsAbove(i) == rows);
        rows += tree.Line(i)->nRows;
    }
    utassert(tree.RowCount() == rows);
    tree.Clear();
    utassert(tree.LineCount() == 0 && tree.RowCount() == 0);
}

static float ByteWidth(void*, Str text) {
    return (float)len(text);
}

struct ProbeMeasure {
    int bytes = 0;
    int longest = 0;
};

static float ProbeByteWidth(void* user, Str text) {
    ProbeMeasure* probe = (ProbeMeasure*)user;
    probe->bytes += len(text);
    if (len(text) > probe->longest) {
        probe->longest = len(text);
    }
    return (float)len(text);
}

static bool SameBreaks(Str text, float width, WrappingIndent indent,
                       WrapMeasureFn measure, void* user) {
    Vec<WrapBoundary> measured;
    Vec<WrapBoundary> original;
    MeasuredWrap(text, width, indent, measure, user, &measured, nullptr);
    MeasuredWrapBoundaries(text, width, indent, measure, user, &original);
    if (len(measured) != len(original)) {
        return false;
    }
    for (int i = 0; i < len(measured); i++) {
        if (!(measured[i] == original[i])) {
            return false;
        }
    }
    return true;
}

// wrap_width/tests.rs oversized_words_keep_measurement_work_local.
static void OversizedWordsKeepMeasurementWorkLocal() {
    const int lengths[] = {8192, 16384};
    const char* prefixes[] = {"", "short ", "    "};
    const float widths[] = {80.f, 256.f, 512.f};
    const WrappingIndent indents[] = {WrappingIndent::None,
                                      WrappingIndent::Same};
    for (int length : lengths) {
        for (const char* prefix : prefixes) {
            int prefixLen = (int)strlen(prefix);
            char* text = (char*)Alloc(nullptr, prefixLen + length + 1);
            memcpy(text, prefix, (size_t)prefixLen);
            memset(text + prefixLen, 'x', (size_t)length);
            text[prefixLen + length] = 0;
            Str line = Str(text, prefixLen + length);
            for (WrappingIndent indent : indents) {
                for (float width : widths) {
                    ProbeMeasure probe;
                    utassert(SameBreaks(line, width, indent, &ProbeByteWidth,
                                        &probe));
                    utassert(probe.longest <= (int)(4.f * width));
                    utassert(probe.bytes <= 32 * len(line));
                }
            }
            Free(nullptr, text);
        }
    }
    utassert(SameBreaks(StrL("hello world"), 8, WrappingIndent::None,
                        &ByteWidth, nullptr));
}

// One grapheme's fractional width, minus the "fi" ligature rust subtracts.
static float FractionalWidth(void*, Str text) {
    float width = 0;
    int at = 0;
    while (at < len(text)) {
        int start = at;
        do {
            uint32_t c = 0;
            int n = Utf8At(text, at, &c);
            at += n > 0 ? n : 1;
        } while (at < len(text) && !TextIsGraphemeBoundary(text, at));
        Str grapheme = Str(text.s + start, at - start);
        bool ascii = true;
        for (int i = 0; i < len(grapheme); i++) {
            if ((unsigned char)grapheme.s[i] >= 0x80) {
                ascii = false;
            }
        }
        if (len(grapheme) == 1 && grapheme.s[0] == '\t') {
            width += 28.25f;
        } else if (ascii) {
            width += 7.125f;
        } else {
            width += 15.75f;
        }
    }
    int fi = 0;
    for (int i = 0; i + 1 < len(text); i++) {
        if (text.s[i] == 'f' && text.s[i + 1] == 'i') {
            fi++;
        }
    }
    return width - (float)fi * 0.875f;
}

// cached_resize_matches_cold_unicode_wrapping_at_fractional_thresholds,
// one logical line at a time: the new probe and the character-first oracle.
static void CachedResizeMatchesColdUnicodeWrapping() {
    const char* text =
        "    alphabet \xE4\xB8\xAD\xE6\x96\x87\xF0\x9F\x98\x80 e\xCC\x81 "
        "family \xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB finish\n"
        "\tfi ffi office\n"
        "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 "
        "\xD8\xA8\xD8\xA7\xD9\x84\xD8\xB9\xD8\xA7\xD9\x84\xD9\x85\n"
        "\r\n"
        "end";
    const WrappingIndent indents[] = {WrappingIndent::None,
                                      WrappingIndent::Same};
    for (WrappingIndent indent : indents) {
        int lineStart = 0;
        for (int i = 0; i <= (int)strlen(text); i++) {
            if (i < (int)strlen(text) && text[i] != '\n') {
                continue;
            }
            int lineEnd = i;
            if (lineEnd > lineStart && text[lineEnd - 1] == '\r') {
                lineEnd--;
            }
            Str line = Str(text + lineStart, lineEnd - lineStart);
            for (int value = 1; value < 240; value++) {
                float base = (float)value / 8.f;
                float widths[] = {base, nextafterf(base, HUGE_VALF),
                                  nextafterf(base, -HUGE_VALF)};
                for (float width : widths) {
                    utassert(SameBreaks(line, width, indent, &FractionalWidth,
                                        nullptr));
                }
            }
            lineStart = i + 1;
        }
    }
}

// set_wrap_width reuses rows while every line still fits, and a width that
// leaves that range matches a cold wrap of the same text.
static void WidthDragReusesMeasuredLines() {
    InputState warm;
    warm.kind = InputKind::Editor;
    warm.softWrap = true;
    InputSetValue(&warm,
                  StrL("short\n"
                       "    a longer line containing words and more words\n"
                       "last\n"));
    InputUpdateWrapMap(&warm, nullptr, 1000, 14, 0);
    uint64_t shaped = warm.wrap.wrappedLines;
    for (int width = 999; width >= 800; width--) {
        InputUpdateWrapMap(&warm, nullptr, (float)width, 14, 0);
    }
    utassert(warm.wrap.wrappedLines == shaped);
    utassert(WrapMatchesFreshWrap(warm, 800));
    InputUpdateWrapMap(&warm, nullptr, 40, 14, 0);
    utassert(warm.wrap.wrappedLines > shaped);
    utassert(WrapMatchesFreshWrap(warm, 40));
    warm.mode.tabSize = 8;
    uint64_t beforeTab = warm.wrap.wrappedLines;
    InputUpdateWrapMap(&warm, nullptr, 40, 14, 0);
    utassert(warm.wrap.wrappedLines > beforeTab);
    utassert(WrapMatchesFreshWrap(warm, 40));
    // A font or indent change drops the measured rows, same as set_font and
    // set_wrapping_indent.
    uint64_t beforeFont = warm.wrap.wrappedLines;
    InputUpdateWrapMap(&warm, nullptr, 40, 22, 0);
    utassert(warm.wrap.wrappedLines > beforeFont);
    utassert(WrapMatchesFreshWrap(warm, 40));
    warm.wrappingIndent = 0;
    uint64_t beforeIndent = warm.wrap.wrappedLines;
    InputUpdateWrapMap(&warm, nullptr, 40, 22, 0);
    utassert(warm.wrap.wrappedLines > beforeIndent);
    utassert(WrapMatchesFreshWrap(warm, 40));
}

void TestTextWrapper() {
    TestSuite("text_wrapper");
    WrapTreeKeepsSuffixStorageAndSummaries();
    MeasuredWrapKeepsCjkLatinBoundaryStableDuringEdits();
    MeasuredWrapPreservesWordsGraphemesAndIndentation();
    WrapLine();
    IsWordChar();
    WrappingIndentSameAndNone();
    EditsRewrapTheLinesTheyTouched();
    OversizedWordsKeepMeasurementWorkLocal();
    CachedResizeMatchesColdUnicodeWrapping();
    WidthDragReusesMeasuredLines();
}
