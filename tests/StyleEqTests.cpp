/* taffy's `#[derive(PartialEq)]` on Style, which src/gpui's layout cache asks
 * the question through: a style that compares equal is a node taffy is not
 * told about, and a node it is not told about keeps last frame's layout.
 *
 * A wrong answer here is a stale frame, and the one way to get one is a field
 * the comparison forgot — so this walks the fields a `gpui::Style` can reach
 * and asserts that moving each of them is visible. */

#include "Test.h"

// `Style` is a name in both namespaces and means different things in each,
// which is why src/taffy has one of its own; here it is taffy's.
using taffy::Dimension;
using taffy::LengthPercentage;
using taffy::LengthPercentageAuto;
using TStyle = taffy::Style;

static void TwoDefaultStylesAreEqual() {
    TStyle a;
    TStyle b;
    utassert(a == b);
    utassert(!(a != b));
}

// One field at a time, over everything the style translation writes.
static void EveryFieldIsCompared() {
    TStyle base;

    {
        TStyle s = base;
        s.display = taffy::Display::Block;
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.boxSizing = taffy::BoxSizing::ContentBox;
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.overflow.x = taffy::Overflow::Scroll;
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.scrollbarWidth = 8.f;
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.position = taffy::Position::Absolute;
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.inset.left = LengthPercentageAuto::Length(4.f);
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.size.width = Dimension::Length(10.f);
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.minSize.height = Dimension::Length(1.f);
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.maxSize.width = Dimension::Percent(0.5f);
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.aspectRatio = 1.5f;
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.margin.top = LengthPercentageAuto::Length(2.f);
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.padding.bottom = LengthPercentage::Length(3.f);
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.border.right = LengthPercentage::Length(1.f);
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.alignItems = taffy::AlignItems{taffy::AlignItemsKeyword::Center};
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.alignSelf = taffy::OptAlignSelf(taffy::AlignSelf{
            taffy::AlignItemsKeyword::Start, taffy::AlignmentSafety::Safe});
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.justifyContent = taffy::OptJustifyContent(
            taffy::JustifyContent{taffy::AlignContentKeyword::SpaceBetween});
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.gap.width = LengthPercentage::Length(8.f);
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.textAlign = taffy::TextAlign::LegacyCenter;
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.flexDirection = taffy::FlexDirection::Column;
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.flexWrap = taffy::FlexWrap::Wrap;
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.flexBasis = Dimension::Length(0.f);
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.flexGrow = 1.f;
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.flexShrink = 0.f;
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.gridAutoFlow = taffy::GridAutoFlow::Column;
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.gridRow.start = taffy::GridPlacement::FromLineIndex(2);
        utassert(!(s == base));
    }
    {
        TStyle s = base;
        s.gridColumn.end = taffy::GridPlacement::FromSpan(3);
        utassert(!(s == base));
    }
}

// A Vec in Rust, so what is compared is the contents and not the address —
// the element tree hands layout a fresh copy of a grid template every frame.
static void ATemplateIsComparedByItsContents() {
    taffy::TrackSizingFunction rowsA[2] = {
        taffy::TrackSizingFunction::Length(10.f),
        taffy::TrackSizingFunction::Auto()};
    taffy::TrackSizingFunction rowsB[2] = {
        taffy::TrackSizingFunction::Length(10.f),
        taffy::TrackSizingFunction::Auto()};
    taffy::TrackSizingFunction rowsC[2] = {
        taffy::TrackSizingFunction::Length(11.f),
        taffy::TrackSizingFunction::Auto()};

    TStyle a;
    TStyle b;
    a.gridAutoRows = taffy::Slice<taffy::TrackSizingFunction>{rowsA, 2};
    b.gridAutoRows = taffy::Slice<taffy::TrackSizingFunction>{rowsB, 2};
    utassert(a == b);

    b.gridAutoRows = taffy::Slice<taffy::TrackSizingFunction>{rowsC, 2};
    utassert(!(a == b));

    b.gridAutoRows = taffy::Slice<taffy::TrackSizingFunction>{rowsA, 1};
    utassert(!(a == b));
}

// `Optf` is Rust's `Option<f32>` with a NaN standing for None. Compared as
// floats, two Nones would come out unequal and every node with no aspect
// ratio would lay out again every frame.
static void TwoNonesAreEqual() {
    TStyle a;
    TStyle b;
    utassert(taffy::IsNone(a.aspectRatio) && taffy::IsNone(b.aspectRatio));
    utassert(a == b);

    a.aspectRatio = 2.f;
    b.aspectRatio = 2.f;
    utassert(a == b);
}

static void RoleOverridesResolveLikeTheSourceEnum() {
    AccessibilityRole role = AccessibilityRole::None;
    utassert(RoleOverride::Implicit()
                 .Resolve(AccessibilityRole::Button, &role));
    utassert(role == AccessibilityRole::Button);
    utassert(!RoleOverride::Presentational()
                  .Resolve(AccessibilityRole::Button, &role));
    utassert(RoleOverride::Explicit(AccessibilityRole::Link)
                 .Resolve(AccessibilityRole::Button, &role));
    utassert(role == AccessibilityRole::Link);
}

static void BoxShadowKeepsEverySourceField() {
    Hsla color = HslaNew(0.5f, 0.4f, 0.3f, 0.8f);
    BoxShadow shadow = box_shadow(1, 2, 3, 4, color);
    utassert(shadow.x == 1 && shadow.y == 2);
    utassert(shadow.blur == 3 && shadow.spread == 4);
    utassert(!shadow.inset);
    utassert(shadow.color.a == HslaToRgba(color).a);
}

static void StyledExtensionsProjectOntoElements() {
    Arena* a = ArenaNew();
    El* row = StyledExt::HFlex(Div(a));
    utassert(row->style.display == Display::Flex);
    utassert(row->style.dir == FlexDir::Row);
    utassert(row->style.align == FlexAlign::Center);

    StyledExt::Paddings(row, Edges::New(1, 2, 3, 4));
    StyledExt::Margins(row, Edges::New(5, 6, 7, 8));
    StyledExt::CornerRadii(row, Corners{9, 10, 11, 12});
    utassert(row->style.pad == Edges::New(1, 2, 3, 4));
    utassert(row->style.margin == Edges::New(5, 6, 7, 8));
    utassert(row->style.corners.tl == 9 && row->style.corners.br == 11);

    StyledExt::FontThin(row);
    utassert(row->style.fontWeight == (uint16_t)FontWeight::Thin);
    StyledExt::FontExtraLight(row);
    utassert(row->style.fontWeight == (uint16_t)FontWeight::ExtraLight);
    StyledExt::FontLight(row);
    utassert(row->style.fontWeight == (uint16_t)FontWeight::Light);
    StyledExt::FontNormal(row);
    utassert(row->style.fontWeight == (uint16_t)FontWeight::Normal);
    StyledExt::FontMedium(row);
    StyledExt::FontSemibold(row);
    StyledExt::FontBold(row);
    StyledExt::FontExtraBold(row);
    StyledExt::FontBlack(row);
    utassert(row->style.fontWeight == (uint16_t)FontWeight::Black);

    int count = 0;
    const char* const* methods = StyledExtReflectionMethods(&count);
    utassert(methods && count == 21);
    utassert(base::StrEq(Str(methods[0]), StrL("refine_style")));
    utassert(base::StrEq(Str(methods[count - 1]), StrL("corner_radii")));
    ArenaDelete(a);
}

static void MarginsReachTaffyAndNormalOverridesItsParent() {
    Arena* a = ArenaNew();
    El* root = Div(a)->FlexRow()->W(100)->H(50)->Bold();
    El* child = Div(a)->W(20)->H(10);
    StyledExt::Margins(child, Edges::New(7, 0, 3, 0));
    StyledExt::FontNormal(child);
    root->Child(child);
    LayoutEl(nullptr, root, 0, 0, 100, 50, 14, Rgba{});
    utassertnear(child->x, 7.f);
    utassertnear(child->y, 3.f);
    utassert(child->style.fontWeight == (uint16_t)FontWeight::Normal);
    ArenaDelete(a);
}

void TestStyleEq() {
    TestSuite("style_eq");
    TwoDefaultStylesAreEqual();
    EveryFieldIsCompared();
    ATemplateIsComparedByItsContents();
    TwoNonesAreEqual();
    RoleOverridesResolveLikeTheSourceEnum();
    BoxShadowKeepsEverySourceField();
    StyledExtensionsProjectOntoElements();
    MarginsReachTaffyAndNormalOverridesItsParent();
}
