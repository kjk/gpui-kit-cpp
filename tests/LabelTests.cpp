/* crates/ui/src/label.rs: highlight ranges and styled-text rendering. */

#include "Test.h"

using namespace gpui::component;

static bool LabelColorEq(Rgba a, Rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static Ctx LabelTestCx(App* app, Arena* a) {
    Ctx cx = {};
    cx.app = app;
    cx.a = a;
    return cx;
}

static void FullMatchesAreCaseInsensitiveAndOverlap() {
    App app = {};
    Arena* a = ArenaNew();
    Ctx cx = LabelTestCx(&app, a);
    Selection ranges[16] = {};

    Label* single = Label::New(&cx, StrL("Hello World"))
                        ->Highlights(StrL("WORLD"));
    int n = single->HighlightRanges(11, ranges, 16);
    utassert(n == 1);
    utassert(ranges[0].start == 6 && ranges[0].end == 11);

    Label* repeated = Label::New(&cx, StrL("Hello Hello Hello"))
                          ->Highlights(StrL("Hello"));
    n = repeated->HighlightRanges(17, ranges, 16);
    utassert(n == 3);
    utassert(ranges[0].start == 0 && ranges[0].end == 5);
    utassert(ranges[1].start == 6 && ranges[1].end == 11);
    utassert(ranges[2].start == 12 && ranges[2].end == 17);

    Label* overlapping = Label::New(&cx, StrL("aaaa"))->Highlights(StrL("aa"));
    n = overlapping->HighlightRanges(4, ranges, 16);
    utassert(n == 3);
    utassert(ranges[0].start == 0 && ranges[0].end == 2);
    utassert(ranges[1].start == 1 && ranges[1].end == 3);
    utassert(ranges[2].start == 2 && ranges[2].end == 4);

    Label* unicode = Label::New(&cx, StrL("你好世界，Hello World"))
                         ->Highlights(StrL("世界"));
    n = unicode->HighlightRanges(len(unicode->text), ranges, 16);
    utassert(n == 1);
    utassert(ranges[0].start == 6 && ranges[0].end == 12);

    ArenaDelete(a);
}

static void PrefixAndSecondaryRangesMatchRust() {
    App app = {};
    Arena* a = ArenaNew();
    Ctx cx = LabelTestCx(&app, a);
    Selection ranges[8] = {};

    HighlightsMatch prefix = HighlightsMatch::Prefix(StrL("hello"));
    utassert(prefix.IsPrefix());
    utassert(base::StrEq(prefix.AsStr(), StrL("hello")));
    utassert(!HighlightsMatch::Full(StrL("hello")).IsPrefix());

    Label* label = Label::New(&cx, StrL("Hello"))
                       ->Secondary(StrL("World"))
                       ->Highlights(prefix);
    Str full = label->FullText();
    utassert(base::StrEq(full, StrL("Hello World")));
    int n = label->HighlightRanges(len(full), ranges, 8);
    utassert(n == 3);
    utassert(ranges[0].start == 0 && ranges[0].end == 5);
    utassert(ranges[1].start == 5 && ranges[1].end == 11);
    utassert(ranges[2].start == 0 && ranges[2].end == 5);

    Label* noPrefix = Label::New(&cx, StrL("xyz Hello"))
                          ->Highlights(HighlightsMatch::Prefix(StrL("Hello")));
    utassert(noPrefix->HighlightRanges(9, ranges, 8) == 0);

    Label* across = Label::New(&cx, StrL("Hello"))
                        ->Secondary(StrL("World"))
                        ->Highlights(StrL("o W"));
    n = across->HighlightRanges(11, ranges, 8);
    utassert(n == 3);
    utassert(ranges[2].start == 4 && ranges[2].end == 7);

    ArenaDelete(a);
}

static void RenderUsesOneStyledRunAndRealBullets() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = LabelTestCx(&app, a);
    const Theme& th = ThemeNow(&app);

    Label* label = Label::New(&cx, StrL("Hello"))
                       ->Secondary(StrL("World"))
                       ->Highlights(StrL("o W"));
    El* root = label->IntoEl();
    El* styled = root->first;
    utassert(styled != nullptr);
    utassert(styled && styled->next == nullptr);
    utassert(styled && base::StrEq(styled->text, StrL("Hello World")));
    utassertnear(root->style.lineHeight, 1.25f);
    utassert(root->style.fontSize == 0);
    utassert(styled && styled->nSpans == 2);
    if (styled && styled->nSpans == 2) {
        utassert(styled->spans[0].lo == 4 && styled->spans[0].hi == 7);
        utassert(LabelColorEq(styled->spans[0].color, th.blue));
        utassert(styled->spans[1].lo == 7 && styled->spans[1].hi == 11);
        utassert(LabelColorEq(styled->spans[1].color, th.mutedFg));
    }

    El* overlap =
        Label::New(&cx, StrL("aaaa"))->Highlights(StrL("aa"))->IntoEl()->first;
    utassert(overlap && overlap->nSpans == 1);
    utassert(overlap && overlap->spans[0].lo == 0);
    utassert(overlap && overlap->spans[0].hi == 4);

    El* masked = Label::New(&cx, StrL("A中"))->Masked(true)->IntoEl()->first;
    utassert(masked != nullptr);
    utassert(masked && len(masked->text) == 6);
    utassert(masked && base::StrEq(masked->text, StrL("••")));

    TempStr longText = AllocStrTemp(80);
    for (int i = 0; i < 80; i++) {
        longText.s[i] = 'x';
    }
    El* longMasked = Label::New(&cx, longText)->Masked(true)->IntoEl()->first;
    utassert(longMasked && len(longMasked->text) == 240);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

void TestLabel() {
    TestSuite("label");
    FullMatchesAreCaseInsensitiveAndOverlap();
    PrefixAndSecondaryRangesMatchRust();
    RenderUsesOneStyledRunAndRealBullets();
}
