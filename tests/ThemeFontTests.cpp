/* crates/component/src/theme/system_font.rs */

#include "Test.h"

static void KeepsTheSystemFontWhenItResolvesToItself() {
    Str installed[] = {StrL("Noto Sans"), StrL(".SystemUIFont")};
    Str got = ThemeSubstituteSystemFont(StrL(".SystemUIFont"),
                                        StrL(".SystemUIFont"), installed, 2);
    utassert(!got.s);
}

static void NamesTheInstalledFamilyTheSystemFontFellBackTo() {
    Str installed[] = {StrL("Noto Sans"), StrL("DejaVu Sans")};
    Str got = ThemeSubstituteSystemFont(StrL(".SystemUIFont"),
                                        StrL("Noto Sans"), installed, 2);
    utassert(StrEq(got, StrL("Noto Sans")));
}

static void KeepsTheSystemFontWhenTheResolvedFamilyIsNotInstalled() {
    Str got = ThemeSubstituteSystemFont(StrL(".SystemUIFont"),
                                        StrL("Noto Sans"), nullptr, 0);
    utassert(!got.s);
    got = ThemeSubstituteSystemFont(StrL(".SystemUIFont"), {}, nullptr, 0);
    utassert(!got.s);
}

void TestThemeFont() {
    TestSuite("theme_font");
    KeepsTheSystemFontWhenItResolvesToItself();
    NamesTheInstalledFamilyTheSystemFontFellBackTo();
    KeepsTheSystemFontWhenTheResolvedFamilyIsNotInstalled();
}
