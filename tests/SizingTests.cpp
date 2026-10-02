/* crates/ui/src/sizing.rs: Size values and StyleSized projections. */

#include "Test.h"

static void NamedAndCustomSizesPreserveTheirValues() {
    utassert(UiSize() == UiSize::Medium);
    utassert(UiSize::Custom(15) == UiSize::Custom(15));
    utassert(UiSize::Custom(15) != UiSize::Custom(16));
    utassert(base::StrEq(UiSizeAsStr(UiSize::XSmall), StrL("xs")));
    utassert(base::StrEq(UiSizeAsStr(UiSize::Small), StrL("sm")));
    utassert(base::StrEq(UiSizeAsStr(UiSize::Medium), StrL("md")));
    utassert(base::StrEq(UiSizeAsStr(UiSize::Large), StrL("lg")));
    utassert(base::StrEq(UiSizeAsStr(UiSize::Custom(15)), StrL("custom")));

    utassert(UiSizeFromStr(StrL("xs")) == UiSize::XSmall);
    utassert(UiSizeFromStr(StrL("xsmall")) == UiSize::XSmall);
    utassert(UiSizeFromStr(StrL("SMALL")) == UiSize::Small);
    utassert(UiSizeFromStr(StrL("Md")) == UiSize::Medium);
    utassert(UiSizeFromStr(StrL("medium")) == UiSize::Medium);
    utassert(UiSizeFromStr(StrL("LG")) == UiSize::Large);
    utassert(UiSizeFromStr(StrL("large")) == UiSize::Large);
    utassert(UiSizeFromStr(StrL("unknown")) == UiSize::Medium);
}

static void MinMaxAndStepsMatchThePinnedDirection() {
    utassert(UiSizeMin(UiSize::Small, UiSize::XSmall) == UiSize::Small);
    utassert(UiSizeMin(UiSize::XSmall, UiSize::Small) == UiSize::Small);
    utassert(UiSizeMin(UiSize::Small, UiSize::Medium) == UiSize::Medium);
    utassert(UiSizeMin(UiSize::Medium, UiSize::Large) == UiSize::Large);
    utassert(UiSizeMin(UiSize::Large, UiSize::Small) == UiSize::Large);
    utassert(UiSizeMin(UiSize::Custom(10), UiSize::Custom(20)) ==
             UiSize::Custom(20));

    utassert(UiSizeMax(UiSize::Small, UiSize::XSmall) == UiSize::XSmall);
    utassert(UiSizeMax(UiSize::XSmall, UiSize::Small) == UiSize::XSmall);
    utassert(UiSizeMax(UiSize::Small, UiSize::Medium) == UiSize::Small);
    utassert(UiSizeMax(UiSize::Medium, UiSize::Large) == UiSize::Medium);
    utassert(UiSizeMax(UiSize::Large, UiSize::Small) == UiSize::Small);
    utassert(UiSizeMax(UiSize::Custom(10), UiSize::Custom(20)) ==
             UiSize::Custom(10));

    utassert(UiSizeSmaller(UiSize::XSmall) == UiSize::XSmall);
    utassert(UiSizeSmaller(UiSize::Large) == UiSize::Medium);
    utassert(UiSizeSmaller(UiSize::Custom(50)) == UiSize::Custom(10));
    utassert(UiSizeLarger(UiSize::XSmall) == UiSize::Small);
    utassert(UiSizeLarger(UiSize::Large) == UiSize::Large);
    UiSize larger = UiSizeLarger(UiSize::Custom(50));
    utassert(larger.kind == UiSize::Kind::Size);
    utassertnear(larger.pixels, 60);
}

static void TableAndInputConstantsAreExact() {
    utassertnear(UiTableRowHeight(UiSize::XSmall), 26);
    utassertnear(UiTableRowHeight(UiSize::Small), 30);
    utassertnear(UiTableRowHeight(UiSize::Medium), 32);
    utassertnear(UiTableRowHeight(UiSize::Large), 40);
    utassertnear(UiTableRowHeight(UiSize::Custom(48)), 48);

    Edges xs = UiTableCellPadding(UiSize::XSmall);
    Edges sm = UiTableCellPadding(UiSize::Small);
    Edges md = UiTableCellPadding(UiSize::Medium);
    Edges lg = UiTableCellPadding(UiSize::Large);
    utassert(xs == Edges::New(4, 4, 2, 2));
    utassert(sm == Edges::New(6, 6, 3, 3));
    utassert(md == Edges::New(8, 8, 4, 4));
    utassert(lg == Edges::New(12, 12, 8, 8));

    utassertnear(UiInputPadX(UiSize::Large), 12);
    utassertnear(UiInputPadY(UiSize::Large), 10);
    utassertnear(UiInputHeight(nullptr, UiSize::XSmall), 20);
    utassertnear(UiInputHeight(nullptr, UiSize::Custom(70)), 24);
    utassertnear(UiInputFontPx(UiSize::XSmall), 12);
    utassertnear(UiInputFontPx(UiSize::Custom(40)), 35);
    utassertnear(UiSizeWithPx(nullptr, UiSize::Large), 44);
    utassertnear(UiSizeWithPx(nullptr, UiSize::Custom(37)), 37);
}

static void StyleSizedHelpersRefineTheElement() {
    Arena* a = ArenaNew();
    El* input = UiInputSize(nullptr, Div(a), UiSize::Small);
    utassertnear(input->style.pad.left, 8);
    utassertnear(input->style.pad.right, 8);
    utassertnear(input->style.pad.top, 2);
    utassertnear(input->style.pad.bottom, 2);
    utassertnear(input->style.height, 24);

    El* list = UiListSize(nullptr, Div(a), UiSize::Large);
    utassertnear(list->style.pad.left, 9);
    utassertnear(list->style.pad.top, 8);
    utassertnear(list->style.fontSize, 16);

    El* custom = UiSizeWith(nullptr, Div(a), UiSize::Custom(37));
    utassertnear(custom->style.width, 37);
    utassertnear(custom->style.height, 37);

    El* cell = UiTableCellSize(Div(a), UiSize::XSmall);
    utassertnear(cell->style.fontSize, 12);
    utassert(cell->style.pad == Edges::New(4, 4, 2, 2));
    El* button = UiButtonTextSize(Div(a), UiSize::Small);
    utassertnear(button->style.fontSize, 14);
    ArenaDelete(a);
}

// h_flex_centers_and_v_flex_stretches_on_the_cross_axis, from
// crates/base/src/styled.rs.
//
// `h_flex` centers on the cross axis while `v_flex` leaves the default
// stretch. The asymmetry is deliberate but easy to trip over, so lock it: a
// column placed in a bare row does not fill the row's height, and one taller
// than the row is centered, so its top — commonly a header — is pushed off
// the top edge and clipped.
static void HFlexCentersAndVFlexStretchesOnTheCrossAxis() {
    Arena* a = ArenaNew();

    El* child = Div(a)->W(20)->H(40)->Shrink0();
    El* overflowing = Div(a)->W(20)->H(140)->Shrink0();
    El* stretch = Div(a)->W(20);
    El* row =
        HFlex(a)->SizeFull()->Child(child)->Child(overflowing)->Child(stretch);
    LayoutEl(nullptr, row, 0, 0, 200, 100, 14, Rgba{});

    // A shorter child of a 100px row sits at (100 - 40) / 2, not at the top,
    // and a height-less child keeps its content height instead of filling.
    utassertnear(child->y, 30.f);
    utassertnear(stretch->h, 0.f);
    // And a child taller than the row is centered too, so its top — a
    // column's header, in a real layout — is pushed off the top edge.
    utassertnear(overflowing->y, -20.f);

    // A width-less child of a column does fill the cross axis.
    El* colStretch = Div(a)->H(20);
    El* col = VFlex(a)->SizeFull()->Child(colStretch);
    LayoutEl(nullptr, col, 0, 0, 200, 100, 14, Rgba{});
    utassertnear(colStretch->w, 200.f);

    // The row says otherwise the one way upstream documents: a full-height
    // column asks for the height, or the row stops centering.
    El* full = Div(a)->W(20)->H(kFill);
    El* stretched = HFlex(a)->SizeFull()->Child(full);
    LayoutEl(nullptr, stretched, 0, 0, 200, 100, 14, Rgba{});
    utassertnear(full->y, 0.f);
    utassertnear(full->h, 100.f);

    ArenaDelete(a);
}

void TestSizing() {
    TestSuite("sizing");
    HFlexCentersAndVFlexStretchesOnTheCrossAxis();
    NamedAndCustomSizesPreserveTheirValues();
    MinMaxAndStepsMatchThePinnedDirection();
    TableAndInputConstantsAreExact();
    StyleSizedHelpersRefineTheElement();
}
