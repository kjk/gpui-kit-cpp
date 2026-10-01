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

// TextStyle::font_family. A name interns once into the id the weight word
// carries; the id cascades through the text style like the weight; and the
// backend draws the run in that family, or in the run's default face when
// the family is not installed.
static void AFontFamilyIsInternedCascadesAndShapes() {
    uint8_t courier = FontFamilyIntern(StrL("Courier New"));
    utassert(courier != 0);
    utassert(FontFamilyIntern(StrL("Courier New")) == courier);
    utassert(FontFamilyIntern(StrL("NoSuchFamilyForGpuiTests")) != courier);
    utassert(FontFamilyIntern(Str{}) == 0);
    utassert(StrEq(FontFamilyName(courier), StrL("Courier New")));
    utassert(len(FontFamilyName(0)) == 0);
    uint16_t word = (uint16_t)((int)kFontMono | (int)kFontTabularNums |
                               (int)kFontWeightBold | FontFamilyBits(courier));
    utassert(FontFamilyOf(word) == courier);
    utassert((word & kFontWeightMask) == kFontWeightBold);
    utassert((word & kFontMono) && (word & kFontTabularNums));

    App app = {};
    app.paint = PaintAppNew();
    Window* win = new Window();
    win->app = &app;
    win->paint.app = &app;
    win->paint.window = win;
    win->paint.pa = app.paint;
    Arena* a = ArenaNew();
    El* inherits = TextEl(a, StrL("iiii"))->Mono();
    El* plain = TextEl(a, StrL("iiii"))->Mono();
    El* root = Div(a)
                   ->FlexCol()
                   ->ItemsStart()
                   ->Child(Div(a)
                               ->FontFamily(StrL("Courier New"))
                               ->Child(Div(a)->Child(inherits)))
                   ->Child(plain);
    LayoutEl(&win->paint, root, 0, 0, 400, 200, 14, Rgba{});
    utassert(inherits->style.fontFamily == courier);
    utassert(plain->style.fontFamily == 0);

    uint16_t mono = kFontMono;
    Size base = MeasureText(&win->paint, StrL("iiii"), 14, 0, false, mono);
    Size missing =
        MeasureText(&win->paint, StrL("iiii"), 14, 0, false,
                    (uint16_t)(mono | FontFamilyBits(FontFamilyIntern(
                                          StrL("NoSuchFamilyForGpuiTests")))));
    utassert(base.w > 0);
#if !GPUI_OS_WASM
    utassertnear(missing.w, base.w);
#endif
#if GPUI_OS_WINDOWS
    // Courier New ships with Windows and advances 0.6 em where Consolas
    // advances 0.55, so the family is what moves the run.
    Size named = MeasureText(&win->paint, StrL("iiii"), 14, 0, false,
                             (uint16_t)(mono | FontFamilyBits(courier)));
    utassert(named.w > base.w + 1.f);
    utassert(inherits->w > plain->w + 1.f);
#endif

    ArenaDelete(a);
    delete win;
    PaintAppFree(app.paint);
    app.paint = nullptr;
}

void TestThemeFont() {
    TestSuite("theme_font");
    AFontFamilyIsInternedCascadesAndShapes();
    KeepsTheSystemFontWhenItResolvesToItself();
    NamesTheInstalledFamilyTheSystemFontFellBackTo();
    KeepsTheSystemFontWhenTheResolvedFamilyIsNotInstalled();
}
