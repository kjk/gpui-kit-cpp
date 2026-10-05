/* unicode-linebreak src/lib.rs examples/tests and tests/test_default.rs. */
#define GPUI_INCLUDE_PRIVATE_API 1
#include "Test.h"
#include "UnicodeLinebreakData.h"

namespace {
using namespace unicode_linebreak;

void ExpectBreaks(Str text, const int* offsets, const BreakOpportunity* kinds,
                  int count) {
    auto iterator = LineBreaks(text);
    LineBreak next;
    for (int i = 0; i < count; i++) {
        utassert(iterator.Next(&next));
        utassert(next.offset == offsets[i]);
        utassert(next.opportunity == kinds[i]);
    }
    utassert(!iterator.Next(&next));
    utassert(!iterator.Next(&next));
}

void Corpus() {
    for (const auto& test : kLineBreakCases) {
        Str text(test.text);
        auto iterator = LineBreaks(text);
        LineBreak next;
        int at = 0;
        while (iterator.Next(&next)) {
            utassert(test.offsets[at] != 0);
            if (!test.offsets[at]) break;
            utassert(next.offset == (uint8_t)test.offsets[at++]);
        }
        utassert(test.offsets[at] == 0);
    }
}
} // namespace

void TestUnicodeLinebreak() {
    using namespace unicode_linebreak;
    TestSuite("unicode-linebreak");
    utassert(BreakProperty(0xA) == BreakClass::LineFeed);
    utassert(BreakProperty(0xDB80) == BreakClass::Surrogate);
    utassert(BreakProperty(0xE01EF) == BreakClass::CombiningMark);
    utassert(BreakProperty(0x10FFFF) == BreakClass::Unknown);
    utassert(BreakProperty(0xFFFFFFFF) == BreakClass::Unknown);
    utassert(BreakProperty(0x2CF3) == BreakClass::Alphabetic);
    const int example[] = {2, 5, 6};
    const BreakOpportunity kinds[] = {BreakOpportunity::Allowed,
                                      BreakOpportunity::Mandatory,
                                      BreakOpportunity::Mandatory};
    ExpectBreaks(StrL("a b \nc"), example, kinds, 3);
    const int hard[] = {3, 5, 7, 10, 14, 18};
    const BreakOpportunity mandatory[] = {
        BreakOpportunity::Mandatory, BreakOpportunity::Mandatory,
        BreakOpportunity::Mandatory, BreakOpportunity::Mandatory,
        BreakOpportunity::Mandatory, BreakOpportunity::Mandatory};
    ExpectBreaks(StrL("a\r\nb\rc\nd\xC2\x85"
                      "e\xE2\x80\xA8"
                      "f\xE2\x80\xA9"),
                 hard, mandatory, 6);
    // The actual crate emits no break for empty text (sot -> eot).
    ExpectBreaks(Str(), nullptr, nullptr, 0);
    const int jamo[] = {2, 12, 13};
    const BreakOpportunity jamoKinds[] = {BreakOpportunity::Allowed,
                                          BreakOpportunity::Allowed,
                                          BreakOpportunity::Mandatory};
    ExpectBreaks(StrL("a \xE1\x84\x80\xE1\x85\xA1\xE1\x86\xA8 b"), jamo,
                 jamoKinds, 3);

    auto iterator = LineBreaks(StrL("a b c"));
    LineBreak next;
    utassert(iterator.Next(&next) && next.offset == 2);
    auto copy = iterator;
    utassert(iterator.Next(&next) && next.offset == 4);
    utassert(copy.Next(&next) && next.offset == 4);
    SafeSplit split = SplitAtSafe(StrL("abcd"));
    utassert(StrEq(split.previous, StrL("a")));
    utassert(StrEq(split.safe, StrL("bcd")));
    split = SplitAtSafe(StrL("\xE4\xB8\x96\xE7\x95\x8C"));
    utassert(len(split.previous) == 0 && len(split.safe) == 6);
    split = SplitAtSafe(Str());
    utassert(len(split.previous) == 0 && len(split.safe) == 0);

    // split_at_safe's upstream documentation example.
    Str sentence = StrL("Not allowed to break within em dashes: - -");
    split = SplitAtSafe(sentence);
    auto full = LineBreaks(sentence);
    auto suffix = LineBreaks(split.safe);
    LineBreak a;
    LineBreak b;
    while (full.Next(&a)) {
        if (a.offset < len(split.previous)) continue;
        utassert(suffix.Next(&b));
        utassert(b.offset == a.offset - len(split.previous));
        utassert(b.opportunity == a.opportunity);
    }
    utassert(!suffix.Next(&b));
    Corpus();
}
