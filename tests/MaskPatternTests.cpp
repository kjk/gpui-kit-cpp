/* Ported from crates/base/src/input/base/mask_pattern.rs, mod tests.
 *
 * `test_is_match` and the `tokens()` halves of the two pattern cases assert on
 * `MaskToken`, which Rust parses into a `Vec` up front. A token here is a pure
 * function of its pattern character, so those become MaskTokenAt checks.
 *
 * `test_normalize_number_input` also asserts that the no-op case returns
 * `Cow::Borrowed` — an allocation this port does not make a promise about, so
 * only the values are checked. */

#include "Test.h"

static Arena* Tmp() {
    return GetTempArena();
}

static bool TokenIs(const MaskPattern& p, int pos, MaskToken want,
                    uint32_t wantSep) {
    MaskToken tok = MaskToken::Any;
    uint32_t sep = 0;
    if (!MaskTokenAt(p, pos, &tok, &sep)) {
        return false;
    }
    return tok == want && sep == wantSep;
}

static void MaskNone() {
    MaskPattern mask;
    utassert(MaskIsNone(mask));
    utassert(MaskIsValid(mask, StrL("1124124ASLDJKljk")));
    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL("hello-world")),
                         StrL("hello-world")));
    utassert(base::StrEq(MaskUnapply(Tmp(), mask, StrL("hello-world")),
                         StrL("hello-world")));
}

static void Pattern1() {
    MaskPattern mask = MaskPatternNew(StrL("(AA)999-999"));

    utassert(TokenIs(mask, 0, MaskToken::Sep, '('));
    utassert(TokenIs(mask, 1, MaskToken::Letter, 0));
    utassert(TokenIs(mask, 2, MaskToken::Letter, 0));
    utassert(TokenIs(mask, 3, MaskToken::Sep, ')'));
    utassert(TokenIs(mask, 4, MaskToken::Digit, 0));
    utassert(TokenIs(mask, 7, MaskToken::Sep, '-'));
    utassert(TokenIs(mask, 10, MaskToken::Digit, 0));
    MaskToken tok = MaskToken::Any;
    uint32_t sep = 0;
    utassert(!MaskTokenAt(mask, 11, &tok, &sep));

    utassert(MaskIsValidAt(mask, '(', 0));
    // A separator is stepped over: the letter after it takes the character.
    utassert(MaskIsValidAt(mask, 'H', 0));
    utassert(!MaskIsValidAt(mask, '3', 0));
    utassert(!MaskIsValidAt(mask, '-', 0));
    utassert(!MaskIsValidAt(mask, ')', 1));
    utassert(MaskIsValidAt(mask, 'H', 1));
    utassert(!MaskIsValidAt(mask, '1', 1));
    utassert(MaskIsValidAt(mask, 'e', 2));
    utassert(MaskIsValidAt(mask, ')', 3));
    utassert(MaskIsValidAt(mask, '1', 3));
    utassert(MaskIsValidAt(mask, '2', 4));

    utassert(MaskIsValid(mask, StrL("(AB)123-456")));

    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL("AB123456")),
                         StrL("(AB)123-456")));
    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL("(AB)123-456")),
                         StrL("(AB)123-456")));
    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL("(AB123456")),
                         StrL("(AB)123-456")));
    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL("AB123-456")),
                         StrL("(AB)123-456")));
    utassert(
        base::StrEq(MaskApply(Tmp(), mask, StrL("AB123-")), StrL("(AB)123-")));
    utassert(
        base::StrEq(MaskApply(Tmp(), mask, StrL("AB123--")), StrL("(AB)123-")));
    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL("AB123-4")),
                         StrL("(AB)123-4")));

    utassert(base::StrEq(MaskUnapply(Tmp(), mask, StrL("(AB)123-456")),
                         StrL("AB123456")));

    utassert(!MaskIsValid(mask, StrL("12AB345")));
    utassert(!MaskIsValid(mask, StrL("(11)123-456")));
    utassert(!MaskIsValid(mask, StrL("##")));
    utassert(MaskIsValid(mask, StrL("(AB)123456")));

    MaskPatternFree(&mask);
}

static void Pattern2() {
    MaskPattern mask = MaskPatternNew(StrL("999-999-******"));
    utassert(TokenIs(mask, 0, MaskToken::Digit, 0));
    utassert(TokenIs(mask, 3, MaskToken::Sep, '-'));
    utassert(TokenIs(mask, 7, MaskToken::Sep, '-'));
    utassert(TokenIs(mask, 8, MaskToken::Any, 0));
    utassert(TokenIs(mask, 13, MaskToken::Any, 0));

    Str masked = MaskApply(Tmp(), mask, StrL("123456A(111)"));
    utassert(base::StrEq(masked, StrL("123-456-A(111)")));
    utassert(
        base::StrEq(MaskUnapply(Tmp(), mask, masked), StrL("123456A(111)")));
    utassert(MaskIsValid(mask, masked));

    MaskPatternFree(&mask);
}

static void NumberWithGroupSeparator() {
    MaskPattern comma = MaskPatternNumber(',');
    utassert(base::StrEq(MaskApply(Tmp(), comma, StrL("1234567")),
                         StrL("1,234,567")));
    utassert(base::StrEq(MaskApply(Tmp(), comma, StrL("1,234,567")),
                         StrL("1,234,567")));
    utassert(base::StrEq(MaskUnapply(Tmp(), comma, StrL("1,234,567")),
                         StrL("1234567")));
    utassert(base::StrEq(MaskApply(Tmp(), comma, StrL("1234567.89")),
                         StrL("1,234,567.89")));
    utassert(base::StrEq(MaskUnapply(Tmp(), comma, StrL("1,234,567.89")),
                         StrL("1234567.89")));

    MaskPattern space = MaskPatternNumber(' ');
    utassert(base::StrEq(MaskApply(Tmp(), space, StrL("1234567")),
                         StrL("1 234 567")));
    utassert(base::StrEq(MaskUnapply(Tmp(), space, StrL("1 234 567")),
                         StrL("1234567")));
    utassert(base::StrEq(MaskApply(Tmp(), space, StrL("1234567.89")),
                         StrL("1 234 567.89")));
    utassert(base::StrEq(MaskUnapply(Tmp(), space, StrL("1 234 567.89")),
                         StrL("1234567.89")));

    MaskPattern none = MaskPatternNumber(0);
    utassert(
        base::StrEq(MaskApply(Tmp(), none, StrL("1234567")), StrL("1234567")));
    utassert(base::StrEq(MaskUnapply(Tmp(), none, StrL("1234567")),
                         StrL("1234567")));
    utassert(base::StrEq(MaskApply(Tmp(), none, StrL("1234567.89")),
                         StrL("1234567.89")));
    utassert(base::StrEq(MaskUnapply(Tmp(), none, StrL("1234567.89")),
                         StrL("1234567.89")));
}

static void NumberWithFractionDigits() {
    MaskPattern mask = MaskPatternNumber(',');
    mask.fraction = 4;
    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL("1234567")),
                         StrL("1,234,567")));
    utassert(base::StrEq(MaskUnapply(Tmp(), mask, StrL("1,234,567")),
                         StrL("1234567")));
    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL("1234567.")),
                         StrL("1,234,567.")));
    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL("1234567.89")),
                         StrL("1,234,567.89")));
    utassert(base::StrEq(MaskUnapply(Tmp(), mask, StrL("1,234,567.890")),
                         StrL("1234567.89")));
    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL("1234567.891")),
                         StrL("1,234,567.891")));
    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL("1234567.891234")),
                         StrL("1,234,567.8912")));

    MaskPattern unlimited = MaskPatternNumber(',');
    utassert(base::StrEq(MaskApply(Tmp(), unlimited, StrL("1234567.1234567")),
                         StrL("1,234,567.1234567")));

    MaskPattern integer = MaskPatternNumber(',');
    integer.fraction = 0;
    utassert(base::StrEq(MaskApply(Tmp(), integer, StrL("1234567.1234567")),
                         StrL("1,234,567")));
}

static void SignedNumbers() {
    MaskPattern mask = MaskPatternNumber(',');
    mask.fraction = 2;

    utassert(MaskIsValid(mask, StrL("-")));
    utassert(MaskIsValid(mask, StrL("-1234567")));
    utassert(MaskIsValid(mask, StrL("-1,234,567")));
    utassert(MaskIsValid(mask, StrL("-1234567.")));
    utassert(MaskIsValid(mask, StrL("-1234567.89")));

    utassert(MaskIsValid(mask, StrL("+")));
    utassert(MaskIsValid(mask, StrL("+1234567")));
    utassert(MaskIsValid(mask, StrL("+1,234,567")));
    utassert(MaskIsValid(mask, StrL("+1234567.")));
    utassert(MaskIsValid(mask, StrL("+1234567.89")));

    // Only one sign is valid
    utassert(!MaskIsValid(mask, StrL("+-")));
    utassert(!MaskIsValid(mask, StrL("-+")));
    utassert(!MaskIsValid(mask, StrL("+-1234567")));

    // No sign is valid in the middle of the number
    utassert(!MaskIsValid(mask, StrL("1,-234,567")));
    utassert(!MaskIsValid(mask, StrL("12-34567.89")));

    // Signs in fractions are invalid
    utassert(!MaskIsValid(mask, StrL("+1234567.-")));

    // The separator does not show up before the sign, i.e. never "-,123"
    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL("-123")), StrL("-123")));

    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL("-1234567")),
                         StrL("-1,234,567")));
    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL("+1234567")),
                         StrL("+1,234,567")));
    utassert(base::StrEq(MaskUnapply(Tmp(), mask, StrL("-1,234,567")),
                         StrL("-1234567")));
    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL("-1234567.")),
                         StrL("-1,234,567.")));
    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL("-1234567.89")),
                         StrL("-1,234,567.89")));
}

static void NumberLeadingDot() {
    MaskPattern mask = MaskPatternNumber(0);
    utassert(MaskIsValid(mask, StrL(".")));
    utassert(MaskIsValid(mask, StrL(".5")));
    utassert(MaskIsValid(mask, StrL("-.")));
    utassert(MaskIsValid(mask, StrL("-.5")));
    utassert(!MaskIsValid(mask, StrL("1.2.3")));
    utassert(!MaskIsValid(mask, StrL("1..")));

    // A bare leading dot is kept as-is (not completed to "0."), so deleting
    // the integer part of "1.2" keeps ".2" and stays editable.
    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL(".")), StrL(".")));
    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL(".5")), StrL(".5")));
    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL("-.5")), StrL("-.5")));
    utassert(base::StrEq(MaskApply(Tmp(), mask, StrL("+.5")), StrL("+.5")));

    MaskPattern comma = MaskPatternNumber(',');
    utassert(base::StrEq(MaskApply(Tmp(), comma, StrL(".5")), StrL(".5")));
    utassert(base::StrEq(MaskApply(Tmp(), comma, StrL("-.5")), StrL("-.5")));
}

static void NormalizeNumber() {
    utassert(base::StrEq(NormalizeNumberInput(Tmp(), StrL("-1,234.5")),
                         StrL("-1,234.5")));
    // Full-width digits
    utassert(
        base::StrEq(NormalizeNumberInput(Tmp(), StrL("０１２３４５６７８９")),
                    StrL("0123456789")));
    // Full-width signs, dot and comma
    utassert(
        base::StrEq(NormalizeNumberInput(Tmp(), StrL("＋1．5")), StrL("+1.5")));
    utassert(base::StrEq(NormalizeNumberInput(Tmp(), StrL("－1，234")),
                         StrL("-1,234")));
    // Minus sign (U+2212)
    utassert(
        base::StrEq(NormalizeNumberInput(Tmp(), StrL("−1.5")), StrL("-1.5")));
    // Ideographic full stop
    utassert(
        base::StrEq(NormalizeNumberInput(Tmp(), StrL("12。5")), StrL("12.5")));
    // Other characters are kept as-is
    utassert(base::StrEq(NormalizeNumberInput(Tmp(), StrL("ab 中 1")),
                         StrL("ab 中 1")));
}

// mask_pattern.rs test_is_valid_with_non_ascii_text. Validity is a
// character count on both sides, so a multi-byte separator or a `*`
// that takes `€` still matches.
static void NonAsciiTextIsValid() {
    MaskPattern date = MaskPatternNew(StrL("9999年99月"));
    utassert(MaskIsValid(date, StrL("2024年")));
    utassert(MaskIsValid(date, StrL("2024年12月")));
    utassert(!MaskIsValid(date, StrL("2024年1x")));
    MaskPatternFree(&date);

    MaskPattern any = MaskPatternNew(StrL("*999"));
    utassert(MaskIsValid(any, StrL("€")));
    utassert(MaskIsValid(any, StrL("€123")));
    MaskPatternFree(&any);
}

static void Placeholder() {
    MaskPattern mask = MaskPatternNew(StrL("(999) 999-9999"));
    utassert(base::StrEq(MaskPlaceholder(Tmp(), mask), StrL("(___) ___-____")));
    MaskPatternFree(&mask);
}

void TestMaskPattern() {
    TestSuite("mask_pattern");
    MaskNone();
    Pattern1();
    Pattern2();
    NumberWithGroupSeparator();
    NumberWithFractionDigits();
    SignedNumbers();
    NumberLeadingDot();
    NormalizeNumber();
    NonAsciiTextIsValid();
    Placeholder();
}
