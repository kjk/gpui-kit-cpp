/* crates/ui/src/icon.rs has no unit tests upstream. These are seam tests for
 * the C++ representation of its IconNamed trait and RenderOnce behavior. */

#include "Test.h"

static void DefaultSizeAndColorAreInherited() {
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;

    El* icon = component::Icon::New(&cx, IconName::Check)->IntoEl();
    El* row = Div(a)->FlexRow()->Font(21)->Fg(Rgba{1, 2, 3, 255})->Child(icon);
    LayoutCache* cache = LayoutCacheNew();
    LayoutEl(nullptr, row, 0, 0, 200, 100, 14, Rgba{}, cache);

    utassertnear(icon->w, 21.f);
    utassertnear(icon->h, 21.f);
    utassert(icon->style.hasColor);
    utassert(icon->style.color.r == 1);
    utassert(icon->style.color.g == 2);
    utassert(icon->style.color.b == 3);

    LayoutCacheFree(cache);
    ArenaDelete(a);
}

static void NamedAndCustomPathsUseTheSameSvgElement() {
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;

    El* named =
        component::Icon::New(&cx, component::IconNamed::From(IconName::Close))
            ->IntoEl();
    utassert(base::StrEq(named->iconPath, StrL("icons/close.svg")));

    El* custom = component::Icon::Empty(&cx)
                     ->Path(StrL("icons/application-logo.svg"))
                     ->Size(UiSize::Large)
                     ->Rotate(0.25f)
                     ->Color(Rgba{10, 20, 30, 255})
                     ->IntoEl();
    utassert(base::StrEq(custom->iconPath, StrL("icons/application-logo.svg")));
    utassertnear(custom->style.width, 24.f);
    utassertnear(custom->style.height, 24.f);
    utassertnear(custom->style.rotate, 0.25f);
    utassert(custom->style.hasColor);
    utassert(custom->style.flexShrink == 0);

    ArenaDelete(a);
}

// icon.rs: test_icon_source_builders_replace_previous_source and
// test_icon_builder_preserves_owned_data_and_transform_on_clone. The last
// `Path` or `Data` call selects the source; a data icon keeps its bytes, its
// size, colour and rotation into the element it renders, and renders them as
// SVG source rather than as an asset path. An Icon here lives one frame and is
// not cloned, so the clone half is the element the builder produces.
static const char kArrowSvg[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" "
    "fill=\"none\" stroke=\"currentColor\"><path d=\"m5 12 7-7 7 7\"/>"
    "<path d=\"M12 19V5\"/></svg>";

static void TheIconSourceIsTheLastPathOrDataSet() {
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;

    component::Icon* icon = component::Icon::New(&cx, IconName::Search)
                                ->Data(Str(kArrowSvg));
    utassert(icon->source == component::IconSource::Data);
    utassert(base::StrEq(icon->data, Str(kArrowSvg)));
    // The bytes are the icon's own copy.
    utassert(icon->data.s != kArrowSvg);

    icon->Path(StrL("icons/replacement.svg"));
    utassert(icon->source == component::IconSource::Path);
    utassert(base::StrEq(icon->path, StrL("icons/replacement.svg")));

    icon->Data(Str(kArrowSvg))->Data(StrL("replacement"));
    utassert(icon->source == component::IconSource::Data);
    utassert(base::StrEq(icon->data, StrL("replacement")));

    icon->Path(Str{});
    utassert(icon->source == component::IconSource::Path && !icon->path.s);

    ArenaDelete(a);
}

static void ADataIconKeepsItsBytesSizeColourAndTransform() {
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;

    // Built from a buffer that does not outlive the call.
    El* e = nullptr;
    {
        char bytes[sizeof(kArrowSvg)];
        memcpy(bytes, kArrowSvg, sizeof(kArrowSvg));
        e = component::Icon::Empty(&cx)
                ->Data(Str(bytes, (int)sizeof(kArrowSvg) - 1))
                ->Size(UiSize::Large)
                ->Color(Rgba{255, 0, 0, 255})
                ->Transform(0.25f)
                ->IntoEl();
        memset(bytes, 0, sizeof(bytes));
    }
    utassert(base::StrEq(e->iconSvg, Str(kArrowSvg)));
    utassertnear(e->style.width, 24.f);
    utassertnear(e->style.height, 24.f);
    utassert(e->style.hasColor && e->style.color.r == 255);
    utassertnear(e->style.rotate, 0.25f);
    // `rotate` replaces any previous transformation.
    El* rotated = component::Icon::Empty(&cx)
                      ->Data(Str(kArrowSvg))
                      ->Transform(0.25f)
                      ->Rotate(0.5f)
                      ->IntoEl();
    utassertnear(rotated->style.rotate, 0.5f);

    // A path icon renders no SVG source, and a data one renders its bytes
    // rather than a path.
    El* pathed = component::Icon::New(&cx, IconName::Search)
                     ->Data(Str(kArrowSvg))
                     ->Path(StrL("icons/search.svg"))
                     ->IntoEl();
    utassert(!pathed->iconSvg.s &&
             base::StrEq(pathed->iconPath, StrL("icons/search.svg")));

    // The source converts through the same reader an asset goes through, and
    // is kept by content: the same bytes answer with the same bytecode.
    int len = 0;
    const uint8_t* ops = SvgDrawOpsForXml(Str(kArrowSvg), &len);
    utassert(ops && len > 0);
    int again = 0;
    utassert(SvgDrawOpsForXml(Str(kArrowSvg), &again) == ops && again == len);
    int bad = 0;
    utassert(SvgDrawOpsForXml(StrL("not svg"), &bad) == nullptr && bad == 0);
    SvgCacheClear();

    ArenaDelete(a);
}

static void PinnedIconAdditionsHaveExactAssetPaths() {
    utassert(base::StrEq(IconNamePath(IconName::ALargeSmall),
                         StrL("icons/a-large-small.svg")));
    utassert(base::StrEq(IconNamePath(IconName::BatteryWarning),
                         StrL("icons/battery-warning.svg")));
    utassert(base::StrEq(IconNamePath(IconName::EllipsisVertical),
                         StrL("icons/ellipsis-vertical.svg")));
    utassert(base::StrEq(IconNamePath(IconName::ResizeCorner),
                         StrL("icons/resize-corner.svg")));
    utassert(base::StrEq(IconNamePath(IconName::SortAscending),
                         StrL("icons/sort-ascending.svg")));
    utassert(
        base::StrEq(IconNamePath(IconName::Undo2), StrL("icons/undo-2.svg")));
    // The old C++ spelling remains a compatibility alias, not a replacement
    // for the pinned Close variant.
    utassert(base::StrEq(IconNamePath(IconName::X), StrL("icons/x.svg")));
}

void TestIcon() {
    TestSuite("icon");
    DefaultSizeAndColorAreInherited();
    NamedAndCustomPathsUseTheSameSvgElement();
    TheIconSourceIsTheLastPathOrDataSet();
    ADataIconKeepsItsBytesSizeColourAndTransform();
    PinnedIconAdditionsHaveExactAssetPaths();
}
