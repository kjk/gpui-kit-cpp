/* The slice equality contract in base.h. The length rejection is inline so
   callers avoid entering the case-insensitive comparison for the common
   mismatch, including two slices that happen to share their first byte. */

#include "Test.h"

static void CaseInsensitiveEqualityRejectsLengthFirst() {
    char text[] = "Alpha";
    Str whole(text, 5);
    Str prefix(text, 4);

    utassert(base::StrEqI(whole, "aLPHA"));
    utassert(!base::StrEqI(prefix, whole));
    utassert(!base::StrEqI(whole, "alpha!"));
}

static void CaseInsensitiveEqualityKeepsEmptySliceSemantics() {
    char empty[] = "";
    utassert(base::StrEqI(Str{}, Str{}));
    utassert(base::StrEqI(Str(empty), ""));
    utassert(base::StrEqI(Str{}, ""));
    utassert(!base::StrEqI(Str{}, "x"));
}

static void ComparisonUsesBytesThenLength() {
    utassert(base::StrCmp(StrL("Alpha"), StrL("Alpha")) == 0);
    utassert(base::StrCmp(StrL("Alpha"), StrL("Beta")) < 0);
    utassert(base::StrCmp(StrL("Beta"), StrL("Alpha")) > 0);
    utassert(base::StrCmp(StrL("Alpha"), StrL("Alphabet")) < 0);
    utassert(base::StrCmp(StrL("Alphabet"), StrL("Alpha")) > 0);
    utassert(base::StrCmp(Str{}, Str{}) == 0);
}

static void SequentialStringLookupsAvoidLengthPrepass() {
    static const char values[] = "Alpha\0beta\0longer value\0";
    Str item = base::SeqStrFirst(values);
    utassert(base::StrEq(item, StrL("Alpha")));
    item = base::SeqStrNext(item);
    utassert(base::StrEq(item, StrL("beta")));
    item = base::SeqStrNext(item);
    utassert(base::StrEq(item, StrL("longer value")));
    item = base::SeqStrNext(item);
    utassert(len(item) == 0);
    utassert(base::SeqStrFirst(nullptr).len == 0);
    utassert(base::SeqStrNext({}).len == 0);

    utassert(base::SeqStrIndex(values, StrL("Alpha")) == 0);
    utassert(base::SeqStrIndex(values, StrL("beta")) == 1);
    utassert(base::SeqStrIndex(values, StrL("BETA")) == -1);
    utassert(base::SeqStrIndexIS(values, StrL("BETA")) == 1);
    utassert(base::SeqStrIndex(values, StrL("longer value")) == 2);
    utassert(base::SeqStrIndexIS(values, StrL("missing")) == -1);
    utassert(base::SeqStrContainsI(values, StrL("alpha")));
    utassert(base::SeqStrContainsI(values, StrL("BETA")));
    utassert(base::SeqStrContainsI(values, StrL("longer value")));
    utassert(!base::SeqStrContainsI(values, StrL("longer")));
    utassert(!base::SeqStrContainsI(values, StrL("missing")));
    utassert(!base::SeqStrContainsI(values, Str{}));
    utassert(!base::SeqStrContainsI(nullptr, StrL("alpha")));
}

static void CaseInsensitivePrefixUsesBothOverloads() {
    Str text = StrL("Alpha");
    utassert(base::StrStartsWithI(text, "aL"));
    utassert(base::StrStartsWithI(text, StrL("ALP")));
    utassert(!base::StrStartsWithI(text, "Alphas"));
    utassert(base::StrStartsWithI(Str{}, ""));
    utassert(!base::StrStartsWithI(Str{}, "a"));
}

static void ReplaceAllReplacesNonOverlappingMatches() {
    utassert(base::StrEq(
        base::StrReplaceAll(StrL("aaaa"), StrL("aa"), StrL("b")), StrL("bb")));
    utassert(base::StrEq(
        base::StrReplaceAll(StrL("one two one"), StrL("one"), StrL("three")),
        StrL("three two three")));
}

static void ReplaceAllHandlesEmptyAndMissingMatches() {
    Str value = StrL("hello");
    Str unchanged = base::StrReplaceAll(value, StrL(""), StrL("x"));
    utassert(unchanged.s == value.s && len(unchanged) == len(value));
    unchanged = base::StrReplaceAll(value, StrL("z"), StrL("x"));
    utassert(unchanged.s == value.s && len(unchanged) == len(value));
    utassert(base::StrEq(base::StrReplaceAll(value, StrL("l"), StrL("")),
                         StrL("heo")));
}

// Nothing to allocate is an empty Str, not an allocation of nothing — and a
// negative length asks for close to 2^64 bytes once it is widened, so it is
// the same answer rather than a terminator written at a negative offset.
static void AllocStrTempRefusesNothingAndLessThanNothing() {
    TempStr none = AllocStrTemp(0);
    utassert(none.s == nullptr && len(none) == 0);
    TempStr negative = AllocStrTemp(-1);
    utassert(negative.s == nullptr && len(negative) == 0);
    TempStr huge = AllocStrTemp(-1000000);
    utassert(huge.s == nullptr && len(huge) == 0);

    // And one byte is one byte, terminated.
    TempStr one = AllocStrTemp(1);
    utassert(one.s != nullptr && len(one) == 1 && one.s[1] == 0);
}

// The answer's length is a product — one difference per match — and a wide
// enough replacement overflows it. That must not size a buffer the copy then
// writes past, so a replacement that cannot be expressed is not made.
static void ReplaceAllRefusesALengthItCannotHold() {
    TempStr many = AllocStrTemp(30000);
    for (int i = 0; i < len(many); i++) {
        many.s[i] = 'a';
    }
    TempStr wide = AllocStrTemp(100000);
    for (int i = 0; i < len(wide); i++) {
        wide.s[i] = 'b';
    }
    // Thirty thousand matches, each a hundred thousand bytes wider: three
    // billion, which is not an int.
    Str answer = base::StrReplaceAll(many, StrL("a"), wide);
    utassert(answer.s == many.s && len(answer) == len(many));

    // The same shape below the limit is still replaced.
    Str fits = base::StrReplaceAll(StrL("aaa"), StrL("a"), StrL("bb"));
    utassert(base::StrEq(fits, StrL("bbbbbb")));
}

static void PrefixSuffixAndFindHelpersHandleBoundaries() {
    Str text = StrL("Alpha beta");
    utassert(base::StrStartsWith(text, "Alpha"));
    utassert(!base::StrStartsWith(text, "alpha"));
    utassert(base::StrStartsWith(text, ""));
    utassert(base::StrEndsWith(text, "beta"));
    utassert(!base::StrEndsWith(text, "Beta"));
    utassert(base::StrEndsWithI(text, "BETA"));
    utassert(base::StrEndsWith(text, ""));
    utassert(base::StrFind(text, StrL("beta")) == 6);
    utassert(base::StrFindI(text, StrL("BETA")) == 6);
    utassert(base::StrFind(text, StrL("")) == -1);
    utassert(base::StrContains(text, StrL("Alpha")));
    utassert(base::StrContainsI(text, StrL("BETA")));
    utassert(!base::StrContains(text, StrL("alpha")));

    // Str and const char* overloads share one affix/find path, including
    // empty/null slices and the case-insensitive pair.
    utassert(base::StrStartsWith(text, StrL("Alpha")) ==
             base::StrStartsWith(text, "Alpha"));
    utassert(base::StrStartsWithI(text, StrL("aL")) ==
             base::StrStartsWithI(text, "aL"));
    utassert(base::StrEndsWith(text, StrL("beta")) ==
             base::StrEndsWith(text, "beta"));
    utassert(base::StrEndsWithI(text, StrL("BETA")) ==
             base::StrEndsWithI(text, "BETA"));
    utassert(base::StrFind(text, StrL("beta")) == base::StrFind(text, "beta"));
    utassert(base::StrFindI(text, StrL("BETA")) ==
             base::StrFindI(text, "BETA"));
    utassert(base::StrFind(text, StrL("gamma")) ==
             base::StrFind(text, "gamma"));
    utassert(base::StrEq(text, StrL("Alpha beta")) ==
             base::StrEq(text, "Alpha beta"));
    utassert(base::StrEqI(text, StrL("ALPHA BETA")) ==
             base::StrEqI(text, "ALPHA BETA"));
    utassert(base::StrStartsWith(Str{}, StrL("")) &&
             base::StrStartsWith(Str{}, ""));
    utassert(!base::StrStartsWith(Str{}, StrL("a")) &&
             !base::StrStartsWith(Str{}, "a"));
    utassert(base::StrFind(text, (const char*)nullptr) == -1);
    utassert(base::StrFindI(Str{}, "x") == -1);
}

static void TrimAsciiReturnsASlice() {
    char text[] = "\f \tHello world\r\n";
    Str trimmed = base::StrTrimAscii(Str(text, (int)sizeof(text) - 1));
    utassert(base::StrEq(trimmed, StrL("Hello world")));
    utassert(trimmed.s == text + 3);
    utassert(base::StrEq(base::StrTrimAscii(StrL(" \t\r\n")), StrL("")));
}

static void BuilderBorrowsThenGrowsLikeAVec() {
    TempStr scratch = AllocStrTemp(4);
    StrBuilder b;
    StrBuilderUseExternalBuffer(b, Str(scratch.s, len(scratch) + 1));
    utassert(b.cap == -4); // the fifth byte is held back for the NUL
    utassert(b.Append(StrL("four")));
    utassert(b.els == scratch.s && scratch.s[4] == 0);

    // Taking borrowed storage copies the result and keeps the scratch bound.
    Str four = b.TakeStr();
    utassert(base::StrEq(four, StrL("four")));
    utassert(four.s != scratch.s && b.els == scratch.s && b.len == 0);
    StrFree(four);

    // The next append past the lent capacity allocates and copies. The caller's
    // buffer remains untouched, and the heap block can be handed over.
    utassert(b.Append(StrL("abcde")));
    utassert(b.els != scratch.s && b.cap > 0);
    utassert(scratch.s[0] == 0);
    Str five = b.TakeStr();
    utassert(base::StrEq(five, StrL("abcde")));
    utassert(b.els == nullptr && b.cap == 0 && b.len == 0);
    StrFree(five);
}

static void BuilderArenaStorageStaysWithTheArena() {
    Arena* a = ArenaNew();
    StrBuilder b(a);
    utassert(b.Reserve(4));
    char* first = b.els;
    utassert(first && b.cap < 0);
    utassert(b.Append(StrL("a string longer than reserve")));
    utassert(b.cap < 0 && b.els != first);
    char* storage = b.els;

    Str result = b.TakeStr();
    utassert(base::StrEq(result, StrL("a string longer than reserve")));
    utassert(result.s != storage);
    utassert(b.els == storage && b.cap < 0 && b.len == 0);
    // Destroying b must not try to free either arena allocation.
    ArenaDelete(a);
}

static void BuilderRemovalKeepsTheTerminator() {
    StrBuilder b;
    utassert(b.Append(StrL("abcd")));
    utassert(b.LastChar() == 'd');
    utassert(b.RemoveAt(1, 2) == 'b');
    utassert(base::StrEq(Str(b.els, b.len), StrL("ad")));
    utassert(b.els[b.len] == 0);
    utassert(b.RemoveLast() == 'd');
    utassert(b.LastChar() == 'a' && b.els[b.len] == 0);
    utassert(b.RemoveLast() == 'a');
    utassert(b.RemoveLast() == 0 && b.LastChar() == 0);
}

static void Dup2PutsBothStringsInOneBlock() {
    Str a, b;
    StrDup2(StrL("id"), StrL("label"), a, b);
    utassert(base::StrEq(a, StrL("id")));
    utassert(base::StrEq(b, StrL("label")));
    utassert(a.s && b.s == a.s + len(a) + 1);
    utassert(a.s[len(a)] == 0 && b.s[b.len] == 0);
    StrFree(a);
}

static void Dup2TreatsNullAsEmptyInsideTheSameBlock() {
    Str a, b;
    StrDup2(Str{}, StrL("x"), a, b);
    utassert(len(a) == 0 && a.s);
    utassert(base::StrEq(b, StrL("x")));
    utassert(b.s == a.s + 1);
    StrFree(a);

    StrDup2(StrL("y"), Str{}, a, b);
    utassert(base::StrEq(a, StrL("y")));
    utassert(b.len == 0 && b.s == a.s + len(a) + 1);
    StrFree(a);
}

static void StartsWithAnyChecksFirstCharInSet() {
    Str s = StrL("+123");
    utassert(StrStartsWithAny(s, "+-"));
    utassert(StrStartsWithAny(s, "+"));
    utassert(!StrStartsWithAny(s, "-"));
    utassert(!StrStartsWithAny(s, "123"));

    Str minus = StrL("-456");
    utassert(StrStartsWithAny(minus, "+-"));
    utassert(!StrStartsWithAny(minus, "+"));
    utassert(StrStartsWithAny(minus, "-"));

    utassert(!StrStartsWithAny(Str{}, "+-"));
    utassert(!StrStartsWithAny(StrL(""), "+-"));
    utassert(!StrStartsWithAny(s, ""));
    utassert(!StrStartsWithAny(s, nullptr));
}

void TestStr() {
    TestSuite("str");
    CaseInsensitiveEqualityRejectsLengthFirst();
    CaseInsensitiveEqualityKeepsEmptySliceSemantics();
    ComparisonUsesBytesThenLength();
    SequentialStringLookupsAvoidLengthPrepass();
    CaseInsensitivePrefixUsesBothOverloads();
    StartsWithAnyChecksFirstCharInSet();
    ReplaceAllReplacesNonOverlappingMatches();
    ReplaceAllHandlesEmptyAndMissingMatches();
    AllocStrTempRefusesNothingAndLessThanNothing();
    ReplaceAllRefusesALengthItCannotHold();
    PrefixSuffixAndFindHelpersHandleBoundaries();
    TrimAsciiReturnsASlice();
    BuilderBorrowsThenGrowsLikeAVec();
    BuilderArenaStorageStaysWithTheArena();
    BuilderRemovalKeepsTheTerminator();
    Dup2PutsBothStringsInOneBlock();
    Dup2TreatsNullAsEmptyInsideTheSameBlock();
}
