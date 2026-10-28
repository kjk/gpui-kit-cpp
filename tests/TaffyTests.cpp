/* Ports of the unit tests inside the taffy 0.13.0 crate:
   src/util/math.rs, src/util/resolve.rs, src/style/alignment.rs,
   src/style/flex.rs, src/style/mod.rs, src/compute/mod.rs and
   src/tree/taffy_tree.rs.

   Taffy's `#[cfg(test)]` modules are the whole of its in-crate suite; the
   generated WPT-derived fixtures live in the crate's `tests/` directory,
   which is not part of the published crate and so is not ported here.

   Tests that only pin Rust-specific facts are left out and named here: the
   `style_sizes` size-of assertions (the C++ structs are laid out
   differently), the `parse`/`serde` cases (neither feature is ported), and
   `new_should_allocate_default_capacity` (the C++ tree's slot vector has no
   capacity to assert on before a node is added). */

// These tests reach the crate's internals (MaybeMin/MaybeMax and friends in
// taffy_math.h), which the amalgam hides from ordinary consumers.
#define GPUI_INCLUDE_PRIVATE_API 1
#include "Test.h"

using namespace taffy;

// Dock's source API also exports a NodeId. This file tests Taffy throughout,
// so keep the intended type explicit at the one boundary that imports both
// gpui and taffy wholesale.
using TaffyNodeId = taffy::NodeId;

// `Style`, `taffy::Overflow`, `taffy::Position` and `taffy::Display` exist in
// both `gpui` and `taffy`, and Test.h pulls all of gpui into scope, so taffy's
// are spelled out at every use below.

// ─── src/util/math.rs ────────────────────────────────────────────────────

static void TestMaybeMathOptOpt() {
    TestSuite("taffy::util::math (Option<f32> op Option<f32>)");

    utassert(OptfEq(MaybeMin(Some(3.0f), Some(5.0f)), Some(3.0f)));
    utassert(OptfEq(MaybeMin(Some(5.0f), Some(3.0f)), Some(3.0f)));
    utassert(OptfEq(MaybeMin(Some(3.0f), None()), Some(3.0f)));
    utassert(OptfEq(MaybeMin(None(), Some(3.0f)), None()));
    utassert(OptfEq(MaybeMin(None(), None()), None()));

    utassert(OptfEq(MaybeMax(Some(3.0f), Some(5.0f)), Some(5.0f)));
    utassert(OptfEq(MaybeMax(Some(5.0f), Some(3.0f)), Some(5.0f)));
    utassert(OptfEq(MaybeMax(Some(3.0f), None()), Some(3.0f)));
    utassert(OptfEq(MaybeMax(None(), Some(3.0f)), None()));
    utassert(OptfEq(MaybeMax(None(), None()), None()));

    utassert(OptfEq(MaybeAdd(Some(3.0f), Some(5.0f)), Some(8.0f)));
    utassert(OptfEq(MaybeAdd(Some(5.0f), Some(3.0f)), Some(8.0f)));
    utassert(OptfEq(MaybeAdd(Some(3.0f), None()), Some(3.0f)));
    utassert(OptfEq(MaybeAdd(None(), Some(3.0f)), None()));
    utassert(OptfEq(MaybeAdd(None(), None()), None()));

    utassert(OptfEq(MaybeSub(Some(3.0f), Some(5.0f)), Some(-2.0f)));
    utassert(OptfEq(MaybeSub(Some(5.0f), Some(3.0f)), Some(2.0f)));
    utassert(OptfEq(MaybeSub(Some(3.0f), None()), Some(3.0f)));
    utassert(OptfEq(MaybeSub(None(), Some(3.0f)), None()));
    utassert(OptfEq(MaybeSub(None(), None()), None()));
}

static void TestMaybeMathOptFloat() {
    TestSuite("taffy::util::math (Option<f32> op f32)");

    utassert(OptfEq(MaybeMin(Some(3.0f), 5.0f), Some(3.0f)));
    utassert(OptfEq(MaybeMin(Some(5.0f), 3.0f), Some(3.0f)));
    utassert(OptfEq(MaybeMin(None(), 3.0f), None()));

    utassert(OptfEq(MaybeMax(Some(3.0f), 5.0f), Some(5.0f)));
    utassert(OptfEq(MaybeMax(Some(5.0f), 3.0f), Some(5.0f)));
    utassert(OptfEq(MaybeMax(None(), 3.0f), None()));

    utassert(OptfEq(MaybeAdd(Some(3.0f), 5.0f), Some(8.0f)));
    utassert(OptfEq(MaybeAdd(Some(5.0f), 3.0f), Some(8.0f)));
    utassert(OptfEq(MaybeAdd(None(), 3.0f), None()));

    utassert(OptfEq(MaybeSub(Some(3.0f), 5.0f), Some(-2.0f)));
    utassert(OptfEq(MaybeSub(Some(5.0f), 3.0f), Some(2.0f)));
    utassert(OptfEq(MaybeSub(None(), 3.0f), None()));
}

static void TestMaybeMathFloatOpt() {
    TestSuite("taffy::util::math (f32 op Option<f32>)");

    utassertnear(MaybeMin(3.0f, Some(5.0f)), 3.0f);
    utassertnear(MaybeMin(5.0f, Some(3.0f)), 3.0f);
    utassertnear(MaybeMin(3.0f, None()), 3.0f);

    utassertnear(MaybeMax(3.0f, Some(5.0f)), 5.0f);
    utassertnear(MaybeMax(5.0f, Some(3.0f)), 5.0f);
    utassertnear(MaybeMax(3.0f, None()), 3.0f);

    utassertnear(MaybeAdd(3.0f, Some(5.0f)), 8.0f);
    utassertnear(MaybeAdd(5.0f, Some(3.0f)), 8.0f);
    utassertnear(MaybeAdd(3.0f, None()), 3.0f);

    utassertnear(MaybeSub(3.0f, Some(5.0f)), -2.0f);
    utassertnear(MaybeSub(5.0f, Some(3.0f)), 2.0f);
    utassertnear(MaybeSub(3.0f, None()), 3.0f);
}

// ─── src/util/resolve.rs ─────────────────────────────────────────────────
//
// Rust's test runners pass `|_, _| 42.42` as the calc resolver; nothing here
// is a calc value, so it never fires. The C++ resolver is a null one.

static CalcResolver NoCalc() {
    return CalcResolver{};
}

static void TestMaybeResolveDimension() {
    TestSuite("taffy::util::resolve (maybe_resolve_dimension)");

    // Auto always resolves to None, whatever the context.
    utassert(OptfEq(Dimension::Auto().MaybeResolve(None(), NoCalc()), None()));
    utassert(
        OptfEq(Dimension::Auto().MaybeResolve(Some(5.0f), NoCalc()), None()));
    utassert(
        OptfEq(Dimension::Auto().MaybeResolve(Some(-5.0f), NoCalc()), None()));
    utassert(
        OptfEq(Dimension::Auto().MaybeResolve(Some(0.0f), NoCalc()), None()));

    // A length is its own value, whatever the context.
    utassert(OptfEq(Dimension::Length(1.0f).MaybeResolve(None(), NoCalc()),
                    Some(1.0f)));
    utassert(OptfEq(Dimension::Length(1.0f).MaybeResolve(Some(5.0f), NoCalc()),
                    Some(1.0f)));
    utassert(OptfEq(Dimension::Length(1.0f).MaybeResolve(Some(-5.0f), NoCalc()),
                    Some(1.0f)));
    utassert(OptfEq(Dimension::Length(1.0f).MaybeResolve(Some(0.0f), NoCalc()),
                    Some(1.0f)));

    // A percentage needs a context, and multiplies by it.
    utassert(OptfEq(Dimension::Percent(1.0f).MaybeResolve(None(), NoCalc()),
                    None()));
    utassert(OptfEq(Dimension::Percent(1.0f).MaybeResolve(Some(5.0f), NoCalc()),
                    Some(5.0f)));
    utassert(
        OptfEq(Dimension::Percent(1.0f).MaybeResolve(Some(-5.0f), NoCalc()),
               Some(-5.0f)));
    utassert(
        OptfEq(Dimension::Percent(1.0f).MaybeResolve(Some(50.0f), NoCalc()),
               Some(50.0f)));

    // b0bb6afc: content is not a length. Off flex-basis it behaves as auto.
    utassert(OptfEq(Dimension::Content().MaybeResolve(Some(100.0f), NoCalc()),
                    None()));
    utassert(!Dimension::Length(1.0f).IsContent());
    utassert(Dimension::Content().IsContent());
}

static void TestMaybeResolveSizeDimension() {
    TestSuite("taffy::util::resolve (maybe_resolve_size_dimension)");

    SizeDim autoSize = SizeDim::Auto();
    utassert(SizeFOptEq(autoSize.MaybeResolve(SizeFOptNone(), NoCalc()),
                        SizeFOptNone()));
    utassert(SizeFOptEq(autoSize.MaybeResolve(SizeFOpt{5.0f, 5.0f}, NoCalc()),
                        SizeFOptNone()));
    utassert(SizeFOptEq(autoSize.MaybeResolve(SizeFOpt{-5.0f, -5.0f}, NoCalc()),
                        SizeFOptNone()));
    utassert(SizeFOptEq(autoSize.MaybeResolve(SizeFOpt{0.0f, 0.0f}, NoCalc()),
                        SizeFOptNone()));

    SizeDim lengths = SizeDim::FromLengths(5.0f, 5.0f);
    utassert(SizeFOptEq(lengths.MaybeResolve(SizeFOptNone(), NoCalc()),
                        SizeFOpt{5.0f, 5.0f}));
    utassert(SizeFOptEq(lengths.MaybeResolve(SizeFOpt{5.0f, 5.0f}, NoCalc()),
                        SizeFOpt{5.0f, 5.0f}));
    utassert(SizeFOptEq(lengths.MaybeResolve(SizeFOpt{-5.0f, -5.0f}, NoCalc()),
                        SizeFOpt{5.0f, 5.0f}));
    utassert(SizeFOptEq(lengths.MaybeResolve(SizeFOpt{0.0f, 0.0f}, NoCalc()),
                        SizeFOpt{5.0f, 5.0f}));

    SizeDim percents = SizeDim::FromPercent(5.0f, 5.0f);
    utassert(SizeFOptEq(percents.MaybeResolve(SizeFOptNone(), NoCalc()),
                        SizeFOptNone()));
    utassert(SizeFOptEq(percents.MaybeResolve(SizeFOpt{5.0f, 5.0f}, NoCalc()),
                        SizeFOpt{25.0f, 25.0f}));
    utassert(SizeFOptEq(percents.MaybeResolve(SizeFOpt{-5.0f, -5.0f}, NoCalc()),
                        SizeFOpt{-25.0f, -25.0f}));
    utassert(SizeFOptEq(percents.MaybeResolve(SizeFOpt{0.0f, 0.0f}, NoCalc()),
                        SizeFOpt{0.0f, 0.0f}));
}

static void TestResolveOrZeroDimension() {
    TestSuite("taffy::util::resolve (resolve_or_zero_dimension)");

    SizeDim autoSize = SizeDim::Auto();
    utassert(autoSize.ResolveOrZero(SizeFOptNone(), NoCalc()) == SizeF::Zero());
    utassert(autoSize.ResolveOrZero(SizeFOpt{5.0f, 5.0f}, NoCalc()) ==
             SizeF::Zero());

    SizeDim lengths = SizeDim::FromLengths(5.0f, 5.0f);
    utassert(lengths.ResolveOrZero(SizeFOptNone(), NoCalc()) ==
             (SizeF{5.0f, 5.0f}));
    utassert(lengths.ResolveOrZero(SizeFOpt{-5.0f, -5.0f}, NoCalc()) ==
             (SizeF{5.0f, 5.0f}));

    SizeDim percents = SizeDim::FromPercent(5.0f, 5.0f);
    utassert(percents.ResolveOrZero(SizeFOptNone(), NoCalc()) == SizeF::Zero());
    utassert(percents.ResolveOrZero(SizeFOpt{5.0f, 5.0f}, NoCalc()) ==
             (SizeF{25.0f, 25.0f}));
    utassert(percents.ResolveOrZero(SizeFOpt{-5.0f, -5.0f}, NoCalc()) ==
             (SizeF{-25.0f, -25.0f}));
    utassert(percents.ResolveOrZero(SizeFOpt{0.0f, 0.0f}, NoCalc()) ==
             SizeF::Zero());
}

static void TestResolveOrZeroRect() {
    TestSuite("taffy::util::resolve (resolve_or_zero_rect)");

    RectLpa autoRect = RectLpa::Auto();
    utassert(autoRect.ResolveOrZero(SizeFOptNone(), NoCalc()) == RectF::Zero());
    utassert(autoRect.ResolveOrZero(SizeFOpt{5.0f, 5.0f}, NoCalc()) ==
             RectF::Zero());

    RectLp lengths = {
        LengthPercentage::Length(5.0f), LengthPercentage::Length(5.0f),
        LengthPercentage::Length(5.0f), LengthPercentage::Length(5.0f)};
    utassert(lengths.ResolveOrZero(SizeFOptNone(), NoCalc()) ==
             RectF::New(5.0f, 5.0f, 5.0f, 5.0f));
    utassert(lengths.ResolveOrZero(SizeFOpt{0.0f, 0.0f}, NoCalc()) ==
             RectF::New(5.0f, 5.0f, 5.0f, 5.0f));

    RectLp percents = {
        LengthPercentage::Percent(5.0f), LengthPercentage::Percent(5.0f),
        LengthPercentage::Percent(5.0f), LengthPercentage::Percent(5.0f)};
    utassert(percents.ResolveOrZero(SizeFOptNone(), NoCalc()) == RectF::Zero());
    utassert(percents.ResolveOrZero(SizeFOpt{5.0f, 5.0f}, NoCalc()) ==
             RectF::New(25.0f, 25.0f, 25.0f, 25.0f));
    utassert(percents.ResolveOrZero(SizeFOpt{-5.0f, -5.0f}, NoCalc()) ==
             RectF::New(-25.0f, -25.0f, -25.0f, -25.0f));
    // The Optf overload resolves every side against the same context.
    utassert(percents.ResolveOrZero(Some(5.0f), NoCalc()) ==
             RectF::New(25.0f, 25.0f, 25.0f, 25.0f));
    utassert(percents.ResolveOrZero(None(), NoCalc()) == RectF::Zero());
}

// ─── src/style/alignment.rs ──────────────────────────────────────────────

static void TestAlignment() {
    TestSuite("taffy::style::alignment");

    constexpr AlignmentSafety kSafe = AlignmentSafety::Safe;

    // align_items_is_safe
    utassert((AlignItems{AlignItemsKeyword::Start, kSafe}).IsSafe());
    utassert((AlignItems{AlignItemsKeyword::End, kSafe}).IsSafe());
    utassert((AlignItems{AlignItemsKeyword::FlexStart, kSafe}).IsSafe());
    utassert((AlignItems{AlignItemsKeyword::FlexEnd, kSafe}).IsSafe());
    utassert((AlignItems{AlignItemsKeyword::Center, kSafe}).IsSafe());
    utassert(!(AlignItems{AlignItemsKeyword::Start}).IsSafe());
    utassert(!(AlignItems{AlignItemsKeyword::End}).IsSafe());
    utassert(!(AlignItems{AlignItemsKeyword::FlexStart}).IsSafe());
    utassert(!(AlignItems{AlignItemsKeyword::FlexEnd}).IsSafe());
    utassert(!(AlignItems{AlignItemsKeyword::Center}).IsSafe());
    utassert(!(AlignItems{AlignItemsKeyword::Baseline}).IsSafe());
    utassert(!(AlignItems{AlignItemsKeyword::Stretch}).IsSafe());
    utassert(!(AlignItems{AlignItemsKeyword::Normal}).IsSafe());
    utassert((AlignItems{AlignItemsKeyword::Normal}).IsStretchOrNormal());
    utassert((AlignItems{AlignItemsKeyword::Stretch}).IsStretchOrNormal());
    utassert(!(AlignItems{AlignItemsKeyword::Start}).IsStretchOrNormal());
    // A bare keyword carries Default, not Unsafe.
    utassert((AlignItems{}).safety == AlignmentSafety::Default);
    utassert((AlignContent{}).safety == AlignmentSafety::Default);

    // align_items_keyword_strips_safe
    utassert((AlignItems{AlignItemsKeyword::Start, kSafe})
                 .Keyword() == AlignItemsKeyword::Start);
    utassert((AlignItems{AlignItemsKeyword::End, kSafe})
                 .Keyword() == AlignItemsKeyword::End);
    utassert((AlignItems{AlignItemsKeyword::FlexStart, kSafe})
                 .Keyword() == AlignItemsKeyword::FlexStart);
    utassert((AlignItems{AlignItemsKeyword::FlexEnd, kSafe})
                 .Keyword() == AlignItemsKeyword::FlexEnd);
    utassert((AlignItems{AlignItemsKeyword::Center, kSafe})
                 .Keyword() == AlignItemsKeyword::Center);

    // align_items_keyword_passthrough
    utassert((AlignItems{AlignItemsKeyword::Start})
                 .Keyword() == AlignItemsKeyword::Start);
    utassert((AlignItems{AlignItemsKeyword::Stretch})
                 .Keyword() == AlignItemsKeyword::Stretch);
    utassert((AlignItems{AlignItemsKeyword::Baseline})
                 .Keyword() == AlignItemsKeyword::Baseline);
    utassert((AlignItems{AlignItemsKeyword::FlexStart})
                 .Keyword() == AlignItemsKeyword::FlexStart);

    // align_content_is_safe
    utassert((AlignContent{AlignContentKeyword::Start, kSafe}).IsSafe());
    utassert((AlignContent{AlignContentKeyword::Center, kSafe}).IsSafe());
    utassert(!(AlignContent{AlignContentKeyword::SpaceBetween}).IsSafe());
    utassert(!(AlignContent{AlignContentKeyword::Stretch}).IsSafe());

    // align_content_keyword_strips_safe
    utassert((AlignContent{AlignContentKeyword::Start, kSafe})
                 .Keyword() == AlignContentKeyword::Start);
    utassert((AlignContent{AlignContentKeyword::FlexEnd, kSafe})
                 .Keyword() == AlignContentKeyword::FlexEnd);
    utassert((AlignContent{AlignContentKeyword::Center, kSafe})
                 .Keyword() == AlignContentKeyword::Center);
    utassert((AlignContent{AlignContentKeyword::SpaceBetween})
                 .Keyword() == AlignContentKeyword::SpaceBetween);

    // align_content_keyword_reversed_swaps_start_end
    utassert(Reversed(AlignContentKeyword::Start) == AlignContentKeyword::End);
    utassert(Reversed(AlignContentKeyword::End) == AlignContentKeyword::Start);
    utassert(Reversed(AlignContentKeyword::FlexStart) ==
             AlignContentKeyword::FlexEnd);
    utassert(Reversed(AlignContentKeyword::FlexEnd) ==
             AlignContentKeyword::FlexStart);
    // Stretch reverses to End, which preserves the pre-refactor behaviour.
    utassert(Reversed(AlignContentKeyword::Stretch) ==
             AlignContentKeyword::End);
    utassert(Reversed(AlignContentKeyword::Normal) ==
             AlignContentKeyword::Normal);
    utassert(Reversed(AlignContentKeyword::Center) ==
             AlignContentKeyword::Center);
    utassert(Reversed(AlignContentKeyword::SpaceBetween) ==
             AlignContentKeyword::SpaceBetween);
    utassert(Reversed(AlignContentKeyword::SpaceEvenly) ==
             AlignContentKeyword::SpaceEvenly);
    utassert(Reversed(AlignContentKeyword::SpaceAround) ==
             AlignContentKeyword::SpaceAround);
}

// ─── src/style/flex.rs ───────────────────────────────────────────────────

static void TestFlexDirection() {
    TestSuite("taffy::style::flex");

    utassert(IsRow(FlexDirection::Row));
    utassert(IsRow(FlexDirection::RowReverse));
    utassert(!IsRow(FlexDirection::Column));
    utassert(!IsRow(FlexDirection::ColumnReverse));

    utassert(!IsColumn(FlexDirection::Row));
    utassert(!IsColumn(FlexDirection::RowReverse));
    utassert(IsColumn(FlexDirection::Column));
    utassert(IsColumn(FlexDirection::ColumnReverse));

    utassert(!IsReverse(FlexDirection::Row));
    utassert(IsReverse(FlexDirection::RowReverse));
    utassert(!IsReverse(FlexDirection::Column));
    utassert(IsReverse(FlexDirection::ColumnReverse));
}

// ─── src/style/mod.rs (defaults_match) ───────────────────────────────────

static void AssertInsetChild(taffy::Position position, taffy::Display display,
                             float x, float y) {
    TaffyTree tree;
    tree.Init();
    taffy::Style childStyle;
    childStyle.position = position;
    childStyle.size = SizeDim::FromLengths(10.0f, 10.0f);
    childStyle.inset.left = LengthPercentageAuto::Length(5.0f);
    childStyle.inset.top = LengthPercentageAuto::Length(7.0f);
    TaffyNodeId child = tree.NewLeaf(childStyle);
    taffy::Style rootStyle;
    rootStyle.display = display;
    rootStyle.size = SizeDim::FromLengths(100.0f, 100.0f);
    TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
    tree.ComputeLayout(root, SizeAvail::MaxContent());
    utassertnear(tree.GetLayout(child).location.x, x);
    utassertnear(tree.GetLayout(child).location.y, y);
    tree.Free();
}

// Static and sticky ignore inset. Relative applies it. Sticky is a
// containing block and stays in flow; static is neither.
static void TestPositionStaticSticky() {
    TestSuite("taffy position static and sticky");
    taffy::Display displays[] = {taffy::Display::Block, taffy::Display::Flex,
                                 taffy::Display::Grid};
    for (int i = 0; i < 3; i++) {
        AssertInsetChild(taffy::Position::Static, displays[i], 0.0f, 0.0f);
        AssertInsetChild(taffy::Position::Sticky, displays[i], 0.0f, 0.0f);
        AssertInsetChild(taffy::Position::Relative, displays[i], 5.0f, 7.0f);
    }
    utassert(IsPositioned(taffy::Position::Sticky));
    utassert(!IsOutOfFlow(taffy::Position::Sticky));
    utassert(!IsPositioned(taffy::Position::Static));
    utassert(IsOutOfFlow(taffy::Position::Fixed));
}

static void TestStyleDefaults() {
    TestSuite("taffy::style (defaults_match)");

    taffy::Style s;
    utassert(s.display == taffy::Display::Flex);
    utassert(!s.itemIsTable);
    utassert(!s.itemIsReplaced);
    utassert(!s.itemIsCompressibleReplaced);
    utassert(s.boxSizing == BoxSizing::BorderBox);
    utassert(s.direction == Direction::Ltr);
    utassert(s.overflow.x == taffy::Overflow::Visible);
    utassert(s.overflow.y == taffy::Overflow::Visible);
    utassertnear(s.scrollbarWidth, 0.0f);
    utassert(s.contain == Contain::None());
    utassert(s.floatMode == Float::None);
    utassert(s.clear == Clear::None);
    utassert(s.position == taffy::Position::Static);
    utassert(s.inset == RectLpa::Auto());
    utassert(s.margin == RectLpa::Zero());
    utassert(s.padding == RectLp::Zero());
    utassert(s.border == RectLp::Zero());
    utassert(s.size == SizeDim::Auto());
    utassert(s.minSize == SizeDim::Auto());
    utassert(s.maxSize == SizeDim::Auto());
    utassert(!IsSome(s.aspectRatio));
    utassert(s.alignItems.keyword == taffy::AlignItemsKeyword::Normal);
    utassert(!s.alignSelf.IsSome());
    utassert(s.justifyItems.keyword == taffy::AlignItemsKeyword::Normal);
    utassert(!s.justifySelf.IsSome());
    utassert(s.alignContent.keyword == taffy::AlignContentKeyword::Normal);
    utassert(s.justifyContent.keyword == taffy::AlignContentKeyword::Normal);
    utassert(s.gap == SizeLp::Zero());
    utassert(s.textAlign == taffy::TextAlign::Auto);
    utassert(s.flexDirection == FlexDirection::Row);
    utassert(s.flexWrap == FlexWrap::NoWrap);
    utassert(s.flexBasis == Dimension::Auto());
    utassertnear(s.flexGrow, 0.0f);
    utassertnear(s.flexShrink, 1.0f);
    utassert(s.gridTemplateRows.IsEmpty());
    utassert(s.gridTemplateColumns.IsEmpty());
    utassert(s.gridAutoRows.IsEmpty());
    utassert(s.gridAutoColumns.IsEmpty());
    utassert(s.gridAutoFlow == GridAutoFlow::Row);
    utassert(s.gridRow.start.kind == GridPlacementKind::Auto);
    utassert(s.gridRow.end.kind == GridPlacementKind::Auto);
    utassert(s.gridColumn.start.kind == GridPlacementKind::Auto);
    utassert(s.gridColumn.end.kind == GridPlacementKind::Auto);
}

// A CompactLength round-trips its tag and its value. This is not one of
// taffy's own tests, but the packing is the one place where the C++ layout
// had to be re-derived rather than translated, so it is pinned here.
static void TestCompactLength() {
    TestSuite("taffy::style::compact_length");

    CompactLength len = CompactLength::Length(12.5f);
    utassert(len.Tag() == CompactLength::kLengthTag);
    utassertnear(len.Value(), 12.5f);
    utassert(!len.IsAuto());
    utassert(len.IsLengthOrPercentage());
    utassert(!len.IsCalc());

    CompactLength pct = CompactLength::Percent(0.25f);
    utassert(pct.Tag() == CompactLength::kPercentTag);
    utassertnear(pct.Value(), 0.25f);
    utassert(pct.UsesPercentage());

    utassert(CompactLength::Auto().IsAuto());
    utassert(CompactLength::MinContent().IsMinContent());
    utassert(CompactLength::MaxContent().IsMaxContent());
    utassert(CompactLength::Fr(2.0f).IsFr());
    utassertnear(CompactLength::Fr(2.0f).Value(), 2.0f);
    utassert(CompactLength::FitContentPx(30.0f).IsFitContent());
    utassert(CompactLength::Zero().IsZero());
    utassert(!CompactLength::Length(1.0f).IsZero());
    utassert(CompactLength::Auto().IsIntrinsic());
    utassert(!CompactLength::Length(1.0f).IsIntrinsic());

    // 446ba1ac: fit-content and stretch on width and height. The keyword is
    // not a fit-content() limit, and neither new tag is intrinsic.
    utassert(CompactLength::FitContentKeyword()
                 .Tag() == CompactLength::kFitContentKeywordTag);
    utassert(CompactLength::Stretch().Tag() == CompactLength::kStretchTag);
    utassert(CompactLength::FitContentKeyword().IsSizingKeyword());
    utassert(CompactLength::Stretch().IsSizingKeyword());
    utassert(CompactLength::MinContent().IsSizingKeyword());
    utassert(CompactLength::FitContentPx(8.0f).IsSizingKeyword());
    utassert(!CompactLength::FitContentKeyword().IsFitContent());
    utassert(!CompactLength::Stretch().IsFitContent());
    utassert(!CompactLength::Stretch().IsIntrinsic());
    utassert(!CompactLength::FitContentKeyword().IsMaxContentAlike());
    utassert(Dimension::Stretch().IsStretch());
    utassert(Dimension::FitContent().IsSizingKeyword());
    utassert(!Dimension::Length(1.0f).IsSizingKeyword());
    utassert(!Dimension::Auto().IsSizingKeyword());
    utassert(MaxTrackSizingFunction::From(Dimension::Stretch()).IsAuto());
    utassert(MaxTrackSizingFunction::From(Dimension::FitContent()).IsAuto());
    utassert(MaxTrackSizingFunction::From(Dimension::MinContent())
                 .IsMinContent());
    utassert(MinTrackSizingFunction::From(Dimension::FitContentPx(10.0f))
                 .IsAuto());
    utassert(MinTrackSizingFunction::From(Dimension::FitContentPercent(0.5f))
                 .IsAuto());
    utassert(MinTrackSizingFunction::From(Dimension::Stretch()).IsAuto());
    utassert(MinTrackSizingFunction::From(Dimension::FitContent()).IsAuto());
    utassert(MinTrackSizingFunction::From(Dimension::MinContent())
                 .IsMinContent());

    // b0bb6afc: content is its own tag. It is not a sizing keyword and not
    // intrinsic. A grid track cannot express it, so both axes become auto.
    utassert(CompactLength::Content().Tag() == CompactLength::kContentTag);
    utassert(CompactLength::Content().IsContent());
    utassert(!CompactLength::Content().IsSizingKeyword());
    utassert(!CompactLength::Content().IsIntrinsic());
    utassert(!CompactLength::Length(1.0f).IsContent());
    utassert(MaxTrackSizingFunction::From(Dimension::Content()).IsAuto());
    utassert(MinTrackSizingFunction::From(Dimension::Content()).IsAuto());

    // 8068a484: percentage is a property of the compact value, including a
    // fit-content() limit. The min half uses that test rather than only
    // a plain percentage tag.
    utassert(!CompactLength::Length(10.0f).UsesPercentage());
    utassert(!CompactLength::Auto().UsesPercentage());
    utassert(!CompactLength::Fr(1.0f).UsesPercentage());
    utassert(!CompactLength::MinContent().UsesPercentage());
    utassert(!CompactLength::MaxContent().UsesPercentage());
    utassert(!CompactLength::FitContentPx(10.0f).UsesPercentage());
    utassert(!CompactLength::FitContentKeyword().UsesPercentage());
    utassert(!CompactLength::Stretch().UsesPercentage());
    utassert(!CompactLength::Content().UsesPercentage());
    utassert(CompactLength::FitContentPercent(0.5f).UsesPercentage());
    utassert(MinTrackSizingFunction::Percent(0.5f).UsesPercentage());
    utassert(!MinTrackSizingFunction::Length(10.0f).UsesPercentage());
    MinTrackSizingFunction fitPctMin;
    fitPctMin.raw = CompactLength::FitContentPercent(0.5f);
    utassert(fitPctMin.UsesPercentage());

    // 49152ca2: fit-content(calc()) is a pointer tag, not a plain calc().
    alignas(8) static const int kCalcMark = 1;
    const void* ptr = &kCalcMark;
    CompactLength fitCalc = CompactLength::FitContentCalc(ptr);
    utassert(fitCalc.Tag() == CompactLength::kFitContentCalcTag);
    utassert(fitCalc.IsFitContentCalc());
    utassert(fitCalc.HasCalc());
    utassert(!fitCalc.IsCalc());
    utassert(fitCalc.CalcValue() == ptr);
    utassert(fitCalc.IsFitContent());
    utassert(fitCalc.IsSizingKeyword());
    utassert(fitCalc.IsMaxOrFitContent());
    utassert(fitCalc.IsMaxContentAlike());
    utassert(fitCalc.IsIntrinsic());
    utassert(fitCalc.UsesPercentage());
    utassert(!fitCalc.IsLengthOrPercentage());
    CompactLength plainCalc = CompactLength::Calc(ptr);
    utassert(plainCalc.IsCalc());
    utassert(plainCalc.HasCalc());
    utassert(!plainCalc.IsFitContentCalc());
    utassert(!plainCalc.IsFitContent());
    utassert(plainCalc.CalcValue() == ptr);
    utassert(plainCalc.UsesPercentage());
    utassert(Dimension::FitContentCalc(ptr).IsFitContentCalc());
    utassert(Dimension::FitContentCalc(ptr).IsSizingKeyword());
    utassert(MinTrackSizingFunction::From(Dimension::FitContentCalc(ptr))
                 .IsAuto());
    utassert(MinTrackSizingFunction::From(
                 MaxTrackSizingFunction::FitContentCalc(ptr))
                 .IsAuto());
    utassert(MaxTrackSizingFunction::FitContent(LengthPercentage::Calc(ptr))
                 .raw == MaxTrackSizingFunction::FitContentCalc(ptr).raw);
    utassert(!MaxTrackSizingFunction::FitContentCalc(ptr)
                  .HasDefiniteValue(Some(100.0f)));
}

// ─── src/tree/taffy_tree.rs ──────────────────────────────────────────────

// The size half of Rust's `size_measure_function`. ComputeLeafLayout applies
// the style around it, which is what that function does after the measure
// signature started returning LayoutOutput.
static SizeF SizeMeasureInner(SizeFOpt knownDimensions,
                              SizeAvail availableSpace, void* nodeContext) {
    (void)availableSpace;
    SizeF ctx = nodeContext ? *(SizeF*)nodeContext : SizeF::Zero();
    return UnwrapOr(knownDimensions, ctx);
}

// "HH" + ZWSP + "HH" from the 446ba1ac fixtures: min-content 20x20,
// max-content 40x10. A definite available size is the fit-content limit.
static SizeF KeywordTextInner(SizeFOpt known, SizeAvail available, void*) {
    const float kMinW = 20.0f;
    const float kMaxW = 40.0f;
    float width;
    if (IsSome(known.w)) {
        width = known.w;
    } else if (available.width.kind == AvailableSpace::Kind::MinContent) {
        width = kMinW;
    } else if (available.width.kind == AvailableSpace::Kind::MaxContent) {
        width = kMaxW;
    } else {
        width = F32Max(kMinW, F32Min(kMaxW, available.width.value));
    }
    float lineH = width + 0.001f < kMaxW ? 20.0f : 10.0f;
    float height;
    if (IsSome(known.h)) {
        height = known.h;
    } else {
        height = lineH;
        if (available.height.kind == AvailableSpace::Kind::Definite) {
            height = F32Max(lineH, F32Min(lineH, available.height.value));
        }
    }
    return {width, height};
}

static taffy::LayoutOutput KeywordTextMeasure(const taffy::LayoutInput& inputs,
                                              TaffyNodeId node,
                                              void* nodeContext,
                                              const taffy::Style* style,
                                              void* userData) {
    (void)node;
    (void)nodeContext;
    (void)userData;
    return taffy::ComputeLeafLayout(inputs, *style, taffy::CalcResolver{},
                                    KeywordTextInner, nullptr);
}

// "XXXX" + ZWSP + "XXXX" from the b0bb6afc flex-basis fixtures: min-content
// width 40, max-content width 80, content height 20 either way.
static SizeF ContentTextInner(SizeFOpt known, SizeAvail available, void*) {
    const float kMinW = 40.0f;
    const float kMaxW = 80.0f;
    float width;
    if (IsSome(known.w)) {
        width = known.w;
    } else if (available.width.kind == AvailableSpace::Kind::MinContent) {
        width = kMinW;
    } else if (available.width.kind == AvailableSpace::Kind::MaxContent) {
        width = kMaxW;
    } else {
        width = F32Max(kMinW, F32Min(kMaxW, available.width.value));
    }
    float height = IsSome(known.h) ? known.h : 20.0f;
    return {width, height};
}

static taffy::LayoutOutput ContentTextMeasure(const taffy::LayoutInput& inputs,
                                              TaffyNodeId node,
                                              void* nodeContext,
                                              const taffy::Style* style,
                                              void* userData) {
    (void)node;
    (void)nodeContext;
    (void)userData;
    return taffy::ComputeLeafLayout(inputs, *style, taffy::CalcResolver{},
                                    ContentTextInner, nullptr);
}

// Three runs of ten "H" separated by ZWSP, Ahem 10px, from the flex-line-count
// tests. A definite inline size wraps on those boundaries.
static SizeF AhemBalanceInner(SizeFOpt known, SizeAvail available, void*) {
    const int kLens[3] = {10, 10, 10};
    const float kH = 10.0f;
    float inlineSize;
    if (IsSome(known.w)) {
        inlineSize = known.w;
    } else if (available.width.kind == AvailableSpace::Kind::MinContent) {
        inlineSize = 100.0f;
    } else if (available.width.kind == AvailableSpace::Kind::MaxContent) {
        inlineSize = 300.0f;
    } else {
        inlineSize = F32Min(available.width.value, 300.0f);
    }
    if (inlineSize < 100.0f) {
        inlineSize = 100.0f;
    }
    float block;
    if (IsSome(known.h)) {
        block = known.h;
    } else {
        int inlineChars = (int)(inlineSize / kH);
        int lineCount = 1;
        int current = 0;
        for (int i = 0; i < 3; i++) {
            if (current + kLens[i] > inlineChars) {
                if (current > 0) {
                    lineCount++;
                }
                current = kLens[i];
            } else {
                current += kLens[i];
            }
        }
        block = (float)lineCount * kH;
    }
    return {inlineSize, block};
}

// Three runs of twelve "H" separated by ZWSP, Ahem 10px. A width of 200px
// wraps to three lines; 300px wraps to two.
static SizeF AhemTwelveInner(SizeFOpt known, SizeAvail available, void*) {
    const int kLens[3] = {12, 12, 12};
    const float kH = 10.0f;
    float inlineSize;
    if (IsSome(known.w)) {
        inlineSize = known.w;
    } else if (available.width.kind == AvailableSpace::Kind::MinContent) {
        inlineSize = 120.0f;
    } else if (available.width.kind == AvailableSpace::Kind::MaxContent) {
        inlineSize = 360.0f;
    } else {
        inlineSize = F32Min(available.width.value, 360.0f);
    }
    if (inlineSize < 120.0f) {
        inlineSize = 120.0f;
    }
    float block;
    if (IsSome(known.h)) {
        block = known.h;
    } else {
        int inlineChars = (int)(inlineSize / kH);
        int lineCount = 1;
        int current = 0;
        for (int i = 0; i < 3; i++) {
            if (current + kLens[i] > inlineChars) {
                if (current > 0) {
                    lineCount++;
                }
                current = kLens[i];
            } else {
                current += kLens[i];
            }
        }
        block = (float)lineCount * kH;
    }
    return {inlineSize, block};
}

// Four runs of two "H" separated by ZWSP, Ahem 10px. The flex-in-grid
// measure-count test (taffy 391e383c). Both known dimensions are already
// answered by ComputeLeafLayout, matching the Rust counter's early return.
static SizeF AhemQuadInner(SizeFOpt known, SizeAvail available, void* ctx) {
    if (IsSome(known.w) && IsSome(known.h)) {
        return {known.w, known.h};
    }
    int* hits = (int*)ctx;
    if (hits) {
        *hits += 1;
    }
    const int kLens[4] = {2, 2, 2, 2};
    const float kH = 10.0f;
    float inlineSize;
    if (IsSome(known.w)) {
        inlineSize = known.w;
    } else if (available.width.kind == AvailableSpace::Kind::MinContent) {
        inlineSize = 20.0f;
    } else if (available.width.kind == AvailableSpace::Kind::MaxContent) {
        inlineSize = 80.0f;
    } else {
        inlineSize = F32Min(available.width.value, 80.0f);
    }
    if (inlineSize < 20.0f) {
        inlineSize = 20.0f;
    }
    float block;
    if (IsSome(known.h)) {
        block = known.h;
    } else {
        int inlineChars = (int)(inlineSize / kH);
        int lineCount = 1;
        int current = 0;
        for (int i = 0; i < 4; i++) {
            if (current + kLens[i] > inlineChars) {
                if (current > 0) {
                    lineCount++;
                }
                current = kLens[i];
            } else {
                current += kLens[i];
            }
        }
        block = (float)lineCount * kH;
    }
    return {inlineSize, block};
}

// Two runs of eight "H" separated by ZWSP, Ahem 10px. The span-2 automatic
// minimum test (taffy a25e1f76).
static SizeF AhemEightPairInner(SizeFOpt known, SizeAvail available, void*) {
    const int kLens[2] = {8, 8};
    const float kH = 10.0f;
    float inlineSize;
    if (IsSome(known.w)) {
        inlineSize = known.w;
    } else if (available.width.kind == AvailableSpace::Kind::MinContent) {
        inlineSize = 80.0f;
    } else if (available.width.kind == AvailableSpace::Kind::MaxContent) {
        inlineSize = 160.0f;
    } else {
        inlineSize = F32Min(available.width.value, 160.0f);
    }
    if (inlineSize < 80.0f) {
        inlineSize = 80.0f;
    }
    float block;
    if (IsSome(known.h)) {
        block = known.h;
    } else {
        int inlineChars = (int)(inlineSize / kH);
        int lineCount = 1;
        int current = 0;
        for (int i = 0; i < 2; i++) {
            if (current + kLens[i] > inlineChars) {
                if (current > 0) {
                    lineCount++;
                }
                current = kLens[i];
            } else {
                current += kLens[i];
            }
        }
        block = (float)lineCount * kH;
    }
    return {inlineSize, block};
}

// Ten runs of two "H" separated by ZWSP, Ahem 10px. Max-content width is
// 200px and one line is 10px, so 80px of available width wraps to 30px.
static SizeF AhemTenPairInner(SizeFOpt known, SizeAvail available, void*) {
    const int kCount = 10;
    const int kWord = 2;
    const float kH = 10.0f;
    const float kMax = 200.0f;
    const float kMin = 20.0f;
    float inlineSize;
    if (IsSome(known.w)) {
        inlineSize = known.w;
    } else if (available.width.kind == AvailableSpace::Kind::MinContent) {
        inlineSize = kMin;
    } else if (available.width.kind == AvailableSpace::Kind::MaxContent) {
        inlineSize = kMax;
    } else {
        inlineSize = F32Min(available.width.value, kMax);
    }
    if (inlineSize < kMin) {
        inlineSize = kMin;
    }
    float block;
    if (IsSome(known.h)) {
        block = known.h;
    } else {
        int inlineChars = (int)(inlineSize / kH);
        if (inlineChars < kWord) {
            inlineChars = kWord;
        }
        int lineCount = 1;
        int current = 0;
        for (int i = 0; i < kCount; i++) {
            if (current + kWord > inlineChars) {
                if (current > 0) {
                    lineCount++;
                }
                current = kWord;
            } else {
                current += kWord;
            }
        }
        block = (float)lineCount * kH;
    }
    return {inlineSize, block};
}

// Three runs of two "H", Ahem 10px. Max-content width is 60px.
static SizeF AhemThreePairInner(SizeFOpt known, SizeAvail available, void*) {
    const float kH = 10.0f;
    const float kMax = 60.0f;
    const float kMin = 20.0f;
    float inlineSize;
    if (IsSome(known.w)) {
        inlineSize = known.w;
    } else if (available.width.kind == AvailableSpace::Kind::MinContent) {
        inlineSize = kMin;
    } else if (available.width.kind == AvailableSpace::Kind::MaxContent) {
        inlineSize = kMax;
    } else {
        inlineSize = F32Min(available.width.value, kMax);
    }
    if (inlineSize < kMin) {
        inlineSize = kMin;
    }
    float block = IsSome(known.h) ? known.h : kH;
    return {inlineSize, block};
}

static taffy::LayoutOutput AhemThreePairMeasure(
    const taffy::LayoutInput& inputs, TaffyNodeId node, void* nodeContext,
    const taffy::Style* style, void* userData) {
    (void)node;
    (void)nodeContext;
    (void)userData;
    return taffy::ComputeLeafLayout(inputs, *style, taffy::CalcResolver{},
                                    AhemThreePairInner, nullptr);
}

static taffy::LayoutOutput AhemTenPairMeasure(const taffy::LayoutInput& inputs,
                                              TaffyNodeId node,
                                              void* nodeContext,
                                              const taffy::Style* style,
                                              void* userData) {
    (void)node;
    (void)nodeContext;
    (void)userData;
    return taffy::ComputeLeafLayout(inputs, *style, taffy::CalcResolver{},
                                    AhemTenPairInner, nullptr);
}

static taffy::LayoutOutput AhemEightPairMeasure(
    const taffy::LayoutInput& inputs, TaffyNodeId node, void* nodeContext,
    const taffy::Style* style, void* userData) {
    (void)node;
    (void)nodeContext;
    (void)userData;
    return taffy::ComputeLeafLayout(inputs, *style, taffy::CalcResolver{},
                                    AhemEightPairInner, nullptr);
}

static taffy::LayoutOutput AhemQuadMeasure(const taffy::LayoutInput& inputs,
                                           TaffyNodeId node, void* nodeContext,
                                           const taffy::Style* style,
                                           void* userData) {
    (void)node;
    (void)userData;
    return taffy::ComputeLeafLayout(inputs, *style, taffy::CalcResolver{},
                                    AhemQuadInner, nodeContext);
}

static SizeF GridWidthSkipInner(SizeFOpt, SizeAvail, void* ctx) {
    int* hits = (int*)ctx;
    *hits += 1;
    return {40.0f, 25.0f};
}

static taffy::LayoutOutput GridWidthSkipMeasure(
    const taffy::LayoutInput& inputs, TaffyNodeId node, void* nodeContext,
    const taffy::Style* style, void* userData) {
    (void)node;
    (void)userData;
    return taffy::ComputeLeafLayout(inputs, *style, taffy::CalcResolver{},
                                    GridWidthSkipInner, nodeContext);
}

static taffy::LayoutOutput AhemTwelveMeasure(const taffy::LayoutInput& inputs,
                                             TaffyNodeId node,
                                             void* nodeContext,
                                             const taffy::Style* style,
                                             void* userData) {
    (void)node;
    (void)nodeContext;
    (void)userData;
    return taffy::ComputeLeafLayout(inputs, *style, taffy::CalcResolver{},
                                    AhemTwelveInner, nullptr);
}

static taffy::LayoutOutput AhemBalanceMeasure(const taffy::LayoutInput& inputs,
                                              TaffyNodeId node,
                                              void* nodeContext,
                                              const taffy::Style* style,
                                              void* userData) {
    (void)node;
    (void)nodeContext;
    (void)userData;
    return taffy::ComputeLeafLayout(inputs, *style, taffy::CalcResolver{},
                                    AhemBalanceInner, nullptr);
}

static void AssertBox(const Layout& layout, float x, float y, float w,
                      float h) {
    utassertnear(layout.location.x, x);
    utassertnear(layout.location.y, y);
    utassertnear(layout.size.w, w);
    utassertnear(layout.size.h, h);
}

static taffy::LayoutOutput SizeMeasureFunction(const taffy::LayoutInput& inputs,
                                               TaffyNodeId node,
                                               void* nodeContext,
                                               const taffy::Style* style,
                                               void* userData) {
    (void)node;
    (void)userData;
    return taffy::ComputeLeafLayout(inputs, *style, taffy::CalcResolver{},
                                    SizeMeasureInner, nodeContext);
}

// A measure that reports its own first baseline. The tree stores that
// output as the leaf's layout, which flex baseline alignment then reads.
struct BaselineContext {
    SizeF size;
    Optf baselineY = None();
};

static taffy::LayoutOutput BaselineMeasure(const taffy::LayoutInput& inputs,
                                           TaffyNodeId node, void* nodeContext,
                                           const taffy::Style* style,
                                           void* userData) {
    (void)inputs;
    (void)node;
    (void)style;
    (void)userData;
    if (!nodeContext) {
        return taffy::LayoutOutput::Hidden();
    }
    BaselineContext* c = (BaselineContext*)nodeContext;
    return taffy::LayoutOutput::FromSizesAndBaselines(
        c->size, RectF::Zero(), taffy::BaselinesFromFirst(c->baselineY));
}

static void TestTaffyTreeBasics() {
    TestSuite("taffy::tree::taffy_tree (nodes)");

    TaffyTree tree;
    tree.Init();

    // test_new_leaf
    TaffyNodeId leaf = tree.NewLeaf(taffy::Style{});
    utassert(tree.ChildCount(leaf) == 0);

    // new_leaf_with_context
    SizeF ctxSize = SizeF::Zero();
    TaffyNodeId ctxLeaf = tree.NewLeafWithContext(taffy::Style{}, &ctxSize);
    utassert(tree.ChildCount(ctxLeaf) == 0);

    // test_new_with_children
    TaffyNodeId child0 = tree.NewLeaf(taffy::Style{});
    TaffyNodeId child1 = tree.NewLeaf(taffy::Style{});
    TaffyNodeId children[] = {child0, child1};
    TaffyNodeId node = tree.NewWithChildren(taffy::Style{}, children, 2);
    utassert(tree.ChildCount(node) == 2);
    utassert(tree.GetChildId(node, 0) == child0);
    utassert(tree.GetChildId(node, 1) == child1);

    // test_child_count
    utassert(tree.ChildCount(child0) == 0);
    utassert(tree.ChildCount(child1) == 0);

    // test_child_at_index
    TaffyNodeId child2 = tree.NewLeaf(taffy::Style{});
    TaffyNodeId three[] = {child0, child1, child2};
    TaffyNodeId node3 = tree.NewWithChildren(taffy::Style{}, three, 3);
    utassert(tree.ChildAtIndex(node3, 0) == child0);
    utassert(tree.ChildAtIndex(node3, 1) == child1);
    utassert(tree.ChildAtIndex(node3, 2) == child2);

    tree.Free();
}

static void TestTaffyTreeHierarchy() {
    TestSuite("taffy::tree::taffy_tree (hierarchy)");

    {
        // remove_node_should_detach_hierarchy
        TaffyTree tree;
        tree.Init();
        TaffyNodeId node2 = tree.NewLeaf(taffy::Style{});
        TaffyNodeId c2[] = {node2};
        TaffyNodeId node1 = tree.NewWithChildren(taffy::Style{}, c2, 1);
        TaffyNodeId c1[] = {node1};
        TaffyNodeId node0 = tree.NewWithChildren(taffy::Style{}, c1, 1);

        utassert(tree.ChildCount(node0) == 1);
        utassert(tree.GetChildId(node0, 0) == node1);
        utassert(tree.ChildCount(node1) == 1);
        utassert(tree.GetChildId(node1, 0) == node2);

        tree.Remove(node1);
        utassert(tree.ChildCount(node0) == 0);
        utassert(tree.ChildCount(node2) == 0);
        tree.Free();
    }

    {
        // add_child
        TaffyTree tree;
        tree.Init();
        TaffyNodeId node = tree.NewLeaf(taffy::Style{});
        utassert(tree.ChildCount(node) == 0);
        tree.AddChild(node, tree.NewLeaf(taffy::Style{}));
        utassert(tree.ChildCount(node) == 1);
        tree.AddChild(node, tree.NewLeaf(taffy::Style{}));
        utassert(tree.ChildCount(node) == 2);
        tree.Free();
    }

    {
        // insert_child_at_index
        TaffyTree tree;
        tree.Init();
        TaffyNodeId child0 = tree.NewLeaf(taffy::Style{});
        TaffyNodeId child1 = tree.NewLeaf(taffy::Style{});
        TaffyNodeId child2 = tree.NewLeaf(taffy::Style{});
        TaffyNodeId node = tree.NewLeaf(taffy::Style{});
        utassert(tree.ChildCount(node) == 0);

        utassert(tree.InsertChildAtIndex(node, 0, child0));
        utassert(tree.ChildCount(node) == 1);
        utassert(tree.GetChildId(node, 0) == child0);

        utassert(tree.InsertChildAtIndex(node, 0, child1));
        utassert(tree.ChildCount(node) == 2);
        utassert(tree.GetChildId(node, 0) == child1);
        utassert(tree.GetChildId(node, 1) == child0);

        utassert(tree.InsertChildAtIndex(node, 1, child2));
        utassert(tree.ChildCount(node) == 3);
        utassert(tree.GetChildId(node, 0) == child1);
        utassert(tree.GetChildId(node, 1) == child2);
        utassert(tree.GetChildId(node, 2) == child0);

        // Past the end is rejected. Rust returns
        // TaffyError::ChildIndexOutOfBounds here.
        utassert(!tree.InsertChildAtIndex(node, 9, child0));
        tree.Free();
    }

    {
        // set_children, and set_children_reparents
        TaffyTree tree;
        tree.Init();
        TaffyNodeId child0 = tree.NewLeaf(taffy::Style{});
        TaffyNodeId child1 = tree.NewLeaf(taffy::Style{});
        TaffyNodeId two[] = {child0, child1};
        TaffyNodeId node = tree.NewWithChildren(taffy::Style{}, two, 2);
        utassert(tree.ChildCount(node) == 2);

        TaffyNodeId child2 = tree.NewLeaf(taffy::Style{});
        TaffyNodeId child3 = tree.NewLeaf(taffy::Style{});
        TaffyNodeId next[] = {child2, child3};
        tree.SetChildren(node, next, 2);
        utassert(tree.ChildCount(node) == 2);
        utassert(tree.GetChildId(node, 0) == child2);
        utassert(tree.GetChildId(node, 1) == child3);

        TaffyNodeId newParent = tree.NewLeaf(taffy::Style{});
        TaffyNodeId one[] = {child2};
        tree.SetChildren(newParent, one, 1);
        utassert(tree.ChildCount(node) == 1);
        utassert(tree.GetChildId(node, 0) == child3);
        tree.Free();
    }

    {
        // remove_child and remove_child_at_index
        TaffyTree tree;
        tree.Init();
        TaffyNodeId child0 = tree.NewLeaf(taffy::Style{});
        TaffyNodeId child1 = tree.NewLeaf(taffy::Style{});
        TaffyNodeId two[] = {child0, child1};
        TaffyNodeId node = tree.NewWithChildren(taffy::Style{}, two, 2);
        utassert(tree.ChildCount(node) == 2);
        tree.RemoveChild(node, child0);
        utassert(tree.ChildCount(node) == 1);
        utassert(tree.GetChildId(node, 0) == child1);
        tree.RemoveChild(node, child1);
        utassert(tree.ChildCount(node) == 0);

        TaffyNodeId again[] = {child0, child1};
        tree.SetChildren(node, again, 2);
        tree.RemoveChildAtIndex(node, 0);
        utassert(tree.ChildCount(node) == 1);
        utassert(tree.GetChildId(node, 0) == child1);
        tree.RemoveChildAtIndex(node, 0);
        utassert(tree.ChildCount(node) == 0);
        tree.Free();
    }

    {
        // remove_children_range
        TaffyTree tree;
        tree.Init();
        TaffyNodeId c[4];
        for (int i = 0; i < 4; i++) {
            c[i] = tree.NewLeaf(taffy::Style{});
        }
        TaffyNodeId node = tree.NewWithChildren(taffy::Style{}, c, 4);
        utassert(tree.ChildCount(node) == 4);

        // Rust's range is 1..=2, which is [1, 3) here.
        tree.RemoveChildrenRange(node, 1, 3);
        utassert(tree.ChildCount(node) == 2);
        utassert(tree.GetChildId(node, 0) == c[0]);
        utassert(tree.GetChildId(node, 1) == c[3]);
        bool hasParent = false;
        tree.Parent(c[0], &hasParent);
        utassert(hasParent);
        tree.Parent(c[3], &hasParent);
        utassert(hasParent);
        tree.Parent(c[1], &hasParent);
        utassert(!hasParent);
        tree.Parent(c[2], &hasParent);
        utassert(!hasParent);
        tree.Free();
    }

    {
        // replace_child_at_index
        TaffyTree tree;
        tree.Init();
        TaffyNodeId child0 = tree.NewLeaf(taffy::Style{});
        TaffyNodeId child1 = tree.NewLeaf(taffy::Style{});
        TaffyNodeId one[] = {child0};
        TaffyNodeId node = tree.NewWithChildren(taffy::Style{}, one, 1);
        utassert(tree.ChildCount(node) == 1);
        utassert(tree.GetChildId(node, 0) == child0);
        tree.ReplaceChildAtIndex(node, 0, child1);
        utassert(tree.ChildCount(node) == 1);
        utassert(tree.GetChildId(node, 0) == child1);
        tree.Free();
    }

    {
        // remove_child_updates_parents — removing the parent must leave the
        // child usable. https://github.com/DioxusLabs/taffy/issues/510
        TaffyTree tree;
        tree.Init();
        TaffyNodeId parent = tree.NewLeaf(taffy::Style{});
        TaffyNodeId child = tree.NewLeaf(taffy::Style{});
        tree.AddChild(parent, child);
        tree.Remove(parent);
        tree.SetChildren(child, nullptr, 0);
        utassert(tree.ChildCount(child) == 0);
        tree.Free();
    }

    {
        // remove() marks the ex-parent dirty, so the next layout drops
        // the removed child's contribution.
        TaffyTree tree;
        tree.Init();
        taffy::Style childStyle;
        childStyle.border.bottom = LengthPercentage::Length(1.0f);
        TaffyNodeId child = tree.NewLeaf(childStyle);
        TaffyNodeId root = tree.NewWithChildren(taffy::Style{}, &child, 1);
        tree.ComputeLayout(root, SizeAvail::MinContent());
        utassertnear(tree.GetLayout(root).size.h, 1.0f);
        tree.Remove(child);
        utassert(tree.Dirty(root));
        tree.ComputeLayout(root, SizeAvail::MinContent());
        utassertnear(tree.GetLayout(root).size.h, 0.0f);
        tree.Free();
    }
}

static void TestTaffyTreeStyleAndDirty() {
    TestSuite("taffy::tree::taffy_tree (style, dirty)");

    {
        // test_set_style / test_style
        TaffyTree tree;
        tree.Init();
        TaffyNodeId node = tree.NewLeaf(taffy::Style{});
        utassert(tree.GetStyle(node).display == taffy::Display::Flex);

        taffy::Style hidden;
        hidden.display = taffy::Display::None;
        tree.SetStyle(node, hidden);
        utassert(tree.GetStyle(node).display == taffy::Display::None);

        taffy::Style reversed;
        reversed.display = taffy::Display::None;
        reversed.flexDirection = FlexDirection::RowReverse;
        TaffyNodeId other = tree.NewLeaf(reversed);
        utassert(tree.GetStyle(other).display == taffy::Display::None);
        utassert(tree.GetStyle(other)
                     .flexDirection == FlexDirection::RowReverse);
        tree.Free();
    }

    {
        // test_mark_dirty
        TaffyTree tree;
        tree.Init();
        TaffyNodeId child0 = tree.NewLeaf(taffy::Style{});
        TaffyNodeId child1 = tree.NewLeaf(taffy::Style{});
        TaffyNodeId two[] = {child0, child1};
        TaffyNodeId node = tree.NewWithChildren(taffy::Style{}, two, 2);

        tree.ComputeLayout(node, SizeAvail::MaxContent());
        utassert(!tree.Dirty(child0));
        utassert(!tree.Dirty(child1));
        utassert(!tree.Dirty(node));

        tree.MarkDirty(node);
        utassert(!tree.Dirty(child0));
        utassert(!tree.Dirty(child1));
        utassert(tree.Dirty(node));

        tree.ComputeLayout(node, SizeAvail::MaxContent());
        tree.MarkDirty(child0);
        utassert(tree.Dirty(child0));
        utassert(!tree.Dirty(child1));
        utassert(tree.Dirty(node));
        tree.Free();
    }
}

static void TestTaffyTreeMeasure() {
    TestSuite("taffy::tree::taffy_tree (measure)");

    {
        // set_measure
        TaffyTree tree;
        tree.Init();
        SizeF bigger = {200.0f, 200.0f};
        TaffyNodeId node = tree.NewLeafWithContext(taffy::Style{}, &bigger);
        tree.ComputeLayoutWithMeasure(node, SizeAvail::MaxContent(),
                                      SizeMeasureFunction, nullptr);
        utassertnear(tree.GetLayout(node).size.w, 200.0f);

        SizeF smaller = {100.0f, 100.0f};
        tree.SetNodeContext(node, &smaller, true);
        tree.ComputeLayoutWithMeasure(node, SizeAvail::MaxContent(),
                                      SizeMeasureFunction, nullptr);
        utassertnear(tree.GetLayout(node).size.w, 100.0f);
        tree.Free();
    }

    {
        // set_measure_of_previously_unmeasured_node
        TaffyTree tree;
        tree.Init();
        TaffyNodeId node = tree.NewLeaf(taffy::Style{});
        tree.ComputeLayoutWithMeasure(node, SizeAvail::MaxContent(),
                                      SizeMeasureFunction, nullptr);
        utassertnear(tree.GetLayout(node).size.w, 0.0f);

        SizeF hundred = {100.0f, 100.0f};
        tree.SetNodeContext(node, &hundred, true);
        tree.ComputeLayoutWithMeasure(node, SizeAvail::MaxContent(),
                                      SizeMeasureFunction, nullptr);
        utassertnear(tree.GetLayout(node).size.w, 100.0f);
        tree.Free();
    }

    {
        // Baselines carries a first baseline and an unset last one.
        taffy::Baselines onlyFirst = taffy::BaselinesFromFirst(Some(4.0f));
        utassert(OptfEq(onlyFirst.first, Some(4.0f)));
        utassert(!IsSome(onlyFirst.last));
        utassert(!IsSome(taffy::BaselinesNone().first));

        // flex_baseline_alignment_uses_measure_function_baseline.
        // The item whose baseline sits closer to its top shifts down so the
        // two baselines meet.
        TaffyTree tree;
        tree.Init();
        BaselineContext aCtx = {{50.0f, 50.0f}, 40.0f};
        BaselineContext bCtx = {{30.0f, 30.0f}, 20.0f};
        TaffyNodeId childA = tree.NewLeafWithContext(taffy::Style{}, &aCtx);
        TaffyNodeId childB = tree.NewLeafWithContext(taffy::Style{}, &bCtx);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Flex;
        rootStyle.flexDirection = FlexDirection::Row;
        rootStyle.alignItems = AlignItems{AlignItemsKeyword::Baseline};
        rootStyle.size = SizeDim::FromLengths(200.0f, 100.0f);
        TaffyNodeId kids[] = {childA, childB};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      BaselineMeasure, nullptr);
        utassertnear(tree.GetLayout(childA).location.y, 0.0f);
        utassertnear(tree.GetLayout(childB).location.y, 20.0f);
        tree.Free();
    }

    {
        // flex_baseline_is_ignored_when_alignment_is_not_baseline.
        TaffyTree tree;
        tree.Init();
        BaselineContext aCtx = {{50.0f, 50.0f}, 40.0f};
        BaselineContext bCtx = {{30.0f, 30.0f}, 20.0f};
        TaffyNodeId childA = tree.NewLeafWithContext(taffy::Style{}, &aCtx);
        TaffyNodeId childB = tree.NewLeafWithContext(taffy::Style{}, &bCtx);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Flex;
        rootStyle.flexDirection = FlexDirection::Row;
        rootStyle.alignItems = AlignItems{AlignItemsKeyword::FlexStart};
        rootStyle.size = SizeDim::FromLengths(200.0f, 100.0f);
        TaffyNodeId kids[] = {childA, childB};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      BaselineMeasure, nullptr);
        utassertnear(tree.GetLayout(childA).location.y, 0.0f);
        utassertnear(tree.GetLayout(childB).location.y, 0.0f);
        tree.Free();
    }

    {
        // align_baseline_child_auto_margin. A child with an auto cross margin
        // does not join baseline alignment. The other two still line up, and
        // the auto margin takes the leftover cross space.
        auto column = [](TaffyTree& tree, float w, float h, float innerH) {
            taffy::Style inner;
            inner.size = SizeDim::FromLengths(w, innerH);
            TaffyNodeId leaf = tree.NewLeaf(inner);
            taffy::Style col;
            col.flexDirection = FlexDirection::Column;
            col.size = SizeDim::FromLengths(w, h);
            return tree.NewWithChildren(col, &leaf, 1);
        };

        TaffyTree tree;
        tree.Init();
        TaffyNodeId a = column(tree, 50.0f, 40.0f, 20.0f);
        TaffyNodeId b = column(tree, 50.0f, 60.0f, 30.0f);
        TaffyNodeId cLeaf;
        {
            taffy::Style inner;
            inner.size = SizeDim::FromLengths(50.0f, 45.0f);
            cLeaf = tree.NewLeaf(inner);
            taffy::Style col;
            col.flexDirection = FlexDirection::Column;
            col.size = SizeDim::FromLengths(50.0f, 50.0f);
            col.margin.top = LengthPercentageAuto::Auto();
            TaffyNodeId c = tree.NewWithChildren(col, &cLeaf, 1);
            taffy::Style rootStyle;
            rootStyle.display = taffy::Display::Flex;
            rootStyle.alignItems = AlignItems{AlignItemsKeyword::Baseline};
            rootStyle.size = SizeDim::FromLengths(200.0f, 100.0f);
            TaffyNodeId kids[] = {a, b, c};
            TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 3);
            tree.ComputeLayout(root, SizeAvail::MaxContent());
            utassertnear(tree.GetLayout(a).location.y, 10.0f);
            utassertnear(tree.GetLayout(b).location.y, 0.0f);
            utassertnear(tree.GetLayout(c).location.y, 50.0f);
            utassertnear(tree.GetLayout(c).size.h, 50.0f);
        }
        tree.Free();

        tree.Init();
        a = column(tree, 50.0f, 40.0f, 20.0f);
        b = column(tree, 50.0f, 60.0f, 30.0f);
        taffy::Style inner;
        inner.size = SizeDim::FromLengths(50.0f, 45.0f);
        cLeaf = tree.NewLeaf(inner);
        taffy::Style col;
        col.flexDirection = FlexDirection::Column;
        col.size = SizeDim::FromLengths(50.0f, 50.0f);
        col.margin.bottom = LengthPercentageAuto::Auto();
        TaffyNodeId c = tree.NewWithChildren(col, &cLeaf, 1);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Flex;
        rootStyle.alignItems = AlignItems{AlignItemsKeyword::Baseline};
        rootStyle.size = SizeDim::FromLengths(200.0f, 100.0f);
        TaffyNodeId kids[] = {a, b, c};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 3);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(a).location.y, 10.0f);
        utassertnear(tree.GetLayout(b).location.y, 0.0f);
        utassertnear(tree.GetLayout(c).location.y, 0.0f);
        tree.Free();
    }
}

static void TestTaffyTreeLayout() {
    TestSuite("taffy::tree::taffy_tree (layout)");

    {
        // compute_layout_should_produce_valid_result
        TaffyTree tree;
        tree.Init();
        taffy::Style s;
        s.size = SizeDim::FromLengths(10.0f, 10.0f);
        TaffyNodeId node = tree.NewLeaf(s);
        tree.ComputeLayout(node, SizeAvail::Definite({100.0f, 100.0f}));
        utassertnear(tree.GetLayout(node).size.w, 10.0f);
        utassertnear(tree.GetLayout(node).size.h, 10.0f);
        tree.Free();
    }

    {
        // make_sure_layout_location_is_top_left. With the root's padding
        // applied, the child sits at {x: 10, y: 30}; any other coordinate
        // space would put it somewhere else.
        TaffyTree tree;
        tree.Init();
        taffy::Style childStyle;
        childStyle.size = SizeDim::FromPercent(1.0f, 1.0f);
        TaffyNodeId node = tree.NewLeaf(childStyle);

        taffy::Style rootStyle;
        rootStyle.size = SizeDim::FromLengths(100.0f, 100.0f);
        rootStyle.padding = {
            LengthPercentage::Length(10.0f), LengthPercentage::Length(20.0f),
            LengthPercentage::Length(30.0f), LengthPercentage::Length(40.0f)};
        TaffyNodeId one[] = {node};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, one, 1);

        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(node).location.x, 10.0f);
        utassertnear(tree.GetLayout(node).location.y, 30.0f);
        tree.Free();
    }
}

// ─── src/compute/mod.rs ──────────────────────────────────────────────────

static void TestHiddenLayout() {
    TestSuite("taffy::compute (hidden_layout_should_hide_recursively)");

    TaffyTree tree;
    tree.Init();

    taffy::Style style;
    style.display = taffy::Display::Flex;
    style.size = SizeDim::FromLengths(50.0f, 50.0f);

    TaffyNodeId grandchild00 = tree.NewLeaf(style);
    TaffyNodeId grandchild01 = tree.NewLeaf(style);
    TaffyNodeId gc0[] = {grandchild00, grandchild01};
    TaffyNodeId child00 = tree.NewWithChildren(style, gc0, 2);

    TaffyNodeId grandchild02 = tree.NewLeaf(style);
    TaffyNodeId gc1[] = {grandchild02};
    TaffyNodeId child01 = tree.NewWithChildren(style, gc1, 1);

    taffy::Style rootStyle;
    rootStyle.display = taffy::Display::None;
    rootStyle.size = SizeDim::FromLengths(50.0f, 50.0f);
    TaffyNodeId kids[] = {child00, child01};
    TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);

    tree.ComputeLayout(root, SizeAvail::MaxContent());

    // Whatever size and display mode the nodes had, every layout resolves to
    // zero because the root is display:none.
    TaffyNodeId all[] = {root,         child00,      child01,
                         grandchild00, grandchild01, grandchild02};
    for (TaffyNodeId n : all) {
        const Layout& l = tree.GetLayout(n);
        utassert(l.size == SizeF::Zero());
        utassert(l.location == PointF::Zero());
    }
    tree.Free();
}

// ─── flexbox end-to-end ──────────────────────────────────────────────────
//
// Not one of taffy's in-crate tests — those live in its unpublished `tests/`
// directory — but the flexbox algorithm is the one gpui's element layout
// runs on, so a handful of the shapes it produces are pinned here.

static void TestFlexboxLayout() {
    TestSuite("taffy::compute::flexbox");

    {
        // Three grow:1 children share a 300px row evenly.
        TaffyTree tree;
        tree.Init();
        taffy::Style childStyle;
        childStyle.flexGrow = 1.0f;
        TaffyNodeId c0 = tree.NewLeaf(childStyle);
        TaffyNodeId c1 = tree.NewLeaf(childStyle);
        TaffyNodeId c2 = tree.NewLeaf(childStyle);
        TaffyNodeId kids[] = {c0, c1, c2};

        taffy::Style rootStyle;
        rootStyle.size = SizeDim::FromLengths(300.0f, 60.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 3);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        utassertnear(tree.GetLayout(root).size.w, 300.0f);
        for (int i = 0; i < 3; i++) {
            const Layout& l = tree.GetLayout(kids[i]);
            utassertnear(l.size.w, 100.0f);
            // align-items defaults to stretch, so each fills the cross axis.
            utassertnear(l.size.h, 60.0f);
            utassertnear(l.location.x, 100.0f * (float)i);
            utassertnear(l.location.y, 0.0f);
        }
        tree.Free();
    }

    {
        // A column with a gap, padding and a fixed-size child.
        TaffyTree tree;
        tree.Init();
        taffy::Style childStyle;
        childStyle.size = SizeDim::FromLengths(40.0f, 20.0f);
        TaffyNodeId c0 = tree.NewLeaf(childStyle);
        TaffyNodeId c1 = tree.NewLeaf(childStyle);
        TaffyNodeId kids[] = {c0, c1};

        taffy::Style rootStyle;
        rootStyle.flexDirection = FlexDirection::Column;
        rootStyle.gap = {LengthPercentage::Length(0.0f),
                         LengthPercentage::Length(8.0f)};
        rootStyle.padding = {
            LengthPercentage::Length(5.0f), LengthPercentage::Length(5.0f),
            LengthPercentage::Length(5.0f), LengthPercentage::Length(5.0f)};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        // 5 + 20 + 8 + 20 + 5
        utassertnear(tree.GetLayout(root).size.h, 58.0f);
        utassertnear(tree.GetLayout(root).size.w, 50.0f);
        utassertnear(tree.GetLayout(c0).location.y, 5.0f);
        utassertnear(tree.GetLayout(c1).location.y, 33.0f);
        utassertnear(tree.GetLayout(c0).location.x, 5.0f);
        tree.Free();
    }

    {
        // justify-content: center and align-items: center place a fixed child
        // in the middle of the container.
        TaffyTree tree;
        tree.Init();
        taffy::Style childStyle;
        childStyle.size = SizeDim::FromLengths(50.0f, 50.0f);
        TaffyNodeId child = tree.NewLeaf(childStyle);
        TaffyNodeId kids[] = {child};

        taffy::Style rootStyle;
        rootStyle.size = SizeDim::FromLengths(200.0f, 100.0f);
        rootStyle.justifyContent = AlignContent{AlignContentKeyword::Center};
        rootStyle.alignItems = AlignItems{AlignItemsKeyword::Center};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        utassertnear(tree.GetLayout(child).location.x, 75.0f);
        utassertnear(tree.GetLayout(child).location.y, 25.0f);
        tree.Free();
    }

    {
        // A shrinking row: two 200px-wide children in a 300px container with
        // the default flex-shrink of 1 end up 150px each.
        TaffyTree tree;
        tree.Init();
        taffy::Style childStyle;
        childStyle.size = SizeDim::FromLengths(200.0f, 20.0f);
        TaffyNodeId c0 = tree.NewLeaf(childStyle);
        TaffyNodeId c1 = tree.NewLeaf(childStyle);
        TaffyNodeId kids[] = {c0, c1};

        taffy::Style rootStyle;
        rootStyle.size = SizeDim::FromLengths(300.0f, 20.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        utassertnear(tree.GetLayout(c0).size.w, 150.0f);
        utassertnear(tree.GetLayout(c1).size.w, 150.0f);
        utassertnear(tree.GetLayout(c1).location.x, 150.0f);
        tree.Free();
    }

    {
        // An absolutely positioned child is placed by its insets and takes no
        // part in the flow.
        TaffyTree tree;
        tree.Init();
        taffy::Style absStyle;
        absStyle.position = taffy::Position::Absolute;
        absStyle.size = SizeDim::FromLengths(30.0f, 30.0f);
        absStyle.inset = {
            LengthPercentageAuto::Length(10.0f), LengthPercentageAuto::Auto(),
            LengthPercentageAuto::Length(20.0f), LengthPercentageAuto::Auto()};
        TaffyNodeId absChild = tree.NewLeaf(absStyle);

        taffy::Style flowStyle;
        flowStyle.size = SizeDim::FromLengths(40.0f, 40.0f);
        TaffyNodeId flowChild = tree.NewLeaf(flowStyle);

        TaffyNodeId kids[] = {absChild, flowChild};
        taffy::Style rootStyle;
        rootStyle.size = SizeDim::FromLengths(200.0f, 100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        utassertnear(tree.GetLayout(absChild).location.x, 10.0f);
        utassertnear(tree.GetLayout(absChild).location.y, 20.0f);
        // The in-flow child still starts at the container's origin.
        utassertnear(tree.GetLayout(flowChild).location.x, 0.0f);
        tree.Free();
    }

    {
        // A column flex item with an indefinite flex basis grows to the
        // container's min-height, but that used height is not definite.
        // The child's percentage height resolves to auto, so it is 0.
        TaffyTree tree;
        tree.Init();
        taffy::Style grandStyle;
        grandStyle.display = taffy::Display::Block;
        grandStyle.size.height = Dimension::Percent(1.0f);
        TaffyNodeId grand = tree.NewLeaf(grandStyle);

        taffy::Style itemStyle;
        itemStyle.display = taffy::Display::Block;
        itemStyle.flexGrow = 1.0f;
        TaffyNodeId itemKids[] = {grand};
        TaffyNodeId item = tree.NewWithChildren(itemStyle, itemKids, 1);

        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Flex;
        rootStyle.flexDirection = FlexDirection::Column;
        rootStyle.size.width = Dimension::Length(200.0f);
        rootStyle.minSize.height = Dimension::Length(200.0f);
        TaffyNodeId rootKids[] = {item};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, rootKids, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        utassertnear(tree.GetLayout(root).size.h, 200.0f);
        utassertnear(tree.GetLayout(item).size.h, 200.0f);
        utassertnear(tree.GetLayout(grand).size.h, 0.0f);
        tree.Free();
    }

    {
        // The same item with a definite flex basis of 200px makes the
        // used height definite, so the child's 100% height is 200.
        TaffyTree tree;
        tree.Init();
        taffy::Style grandStyle;
        grandStyle.display = taffy::Display::Block;
        grandStyle.size.height = Dimension::Percent(1.0f);
        TaffyNodeId grand = tree.NewLeaf(grandStyle);

        taffy::Style itemStyle;
        itemStyle.display = taffy::Display::Block;
        itemStyle.flexGrow = 1.0f;
        itemStyle.flexBasis = Dimension::Length(200.0f);
        TaffyNodeId itemKids[] = {grand};
        TaffyNodeId item = tree.NewWithChildren(itemStyle, itemKids, 1);

        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Flex;
        rootStyle.flexDirection = FlexDirection::Column;
        rootStyle.size.width = Dimension::Length(200.0f);
        rootStyle.minSize.height = Dimension::Length(200.0f);
        TaffyNodeId rootKids[] = {item};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, rootKids, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        utassertnear(tree.GetLayout(root).size.h, 200.0f);
        utassertnear(tree.GetLayout(item).size.h, 200.0f);
        utassertnear(tree.GetLayout(grand).size.h, 200.0f);
        tree.Free();
    }

    {
        // An indefinite width with max-width 20 wraps a row against that
        // max, not against the 200px available space. The 110px height
        // then stretches the two lines, so the second starts at y=40.
        Direction directions[2] = {Direction::Ltr, Direction::Rtl};
        for (int i = 0; i < 2; i++) {
            TaffyTree tree;
            tree.Init();
            taffy::Style aStyle;
            aStyle.boxSizing = BoxSizing::ContentBox;
            aStyle.direction = directions[i];
            aStyle.size = SizeDim::FromLengths(20.0f, 10.0f);
            TaffyNodeId a = tree.NewLeaf(aStyle);
            taffy::Style bStyle;
            bStyle.boxSizing = BoxSizing::ContentBox;
            bStyle.direction = directions[i];
            bStyle.size = SizeDim::FromLengths(20.0f, 40.0f);
            TaffyNodeId b = tree.NewLeaf(bStyle);

            taffy::Style rootStyle;
            rootStyle.boxSizing = BoxSizing::ContentBox;
            rootStyle.direction = directions[i];
            rootStyle.flexWrap = FlexWrap::Wrap;
            rootStyle.size.height = Dimension::Length(110.0f);
            rootStyle.maxSize.width = Dimension::Length(20.0f);
            TaffyNodeId kids[2] = {a, b};
            TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
            SizeAvail space = {AvailableSpace::Definite(200.0f),
                               AvailableSpace::MaxContent()};
            tree.ComputeLayout(root, space);

            utassertnear(tree.GetLayout(root).size.w, 20.0f);
            utassertnear(tree.GetLayout(root).size.h, 110.0f);
            utassertnear(tree.GetLayout(a).location.x, 0.0f);
            utassertnear(tree.GetLayout(a).location.y, 0.0f);
            utassertnear(tree.GetLayout(a).size.w, 20.0f);
            utassertnear(tree.GetLayout(a).size.h, 10.0f);
            utassertnear(tree.GetLayout(b).location.x, 0.0f);
            utassertnear(tree.GetLayout(b).location.y, 40.0f);
            utassertnear(tree.GetLayout(b).size.w, 20.0f);
            utassertnear(tree.GetLayout(b).size.h, 40.0f);
            tree.Free();
        }
    }

    {
        // width:stretch fills the row after a flex-shrink:0 sibling takes
        // its 30px. flex_item_width_stretch, content-box.
        Direction directions[2] = {Direction::Ltr, Direction::Rtl};
        float stretchX[2] = {0.0f, 30.0f};
        float fixedX[2] = {120.0f, 0.0f};
        for (int i = 0; i < 2; i++) {
            TaffyTree tree;
            tree.Init();
            taffy::Style stretchStyle;
            stretchStyle.boxSizing = BoxSizing::ContentBox;
            stretchStyle.direction = directions[i];
            stretchStyle.size.width = Dimension::Stretch();
            stretchStyle.size.height = Dimension::Length(10.0f);
            TaffyNodeId stretch = tree.NewLeaf(stretchStyle);
            taffy::Style fixedStyle;
            fixedStyle.boxSizing = BoxSizing::ContentBox;
            fixedStyle.direction = directions[i];
            fixedStyle.flexShrink = 0.0f;
            fixedStyle.size.width = Dimension::Length(30.0f);
            TaffyNodeId fixed = tree.NewLeaf(fixedStyle);

            taffy::Style rootStyle;
            rootStyle.boxSizing = BoxSizing::ContentBox;
            rootStyle.direction = directions[i];
            rootStyle.size.width = Dimension::Length(150.0f);
            TaffyNodeId kids[2] = {stretch, fixed};
            TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
            tree.ComputeLayout(root, SizeAvail::MaxContent());
            AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 150.0f, 10.0f);
            AssertBox(tree.GetLayout(stretch), stretchX[i], 0.0f, 120.0f,
                      10.0f);
            AssertBox(tree.GetLayout(fixed), fixedX[i], 0.0f, 30.0f, 10.0f);
            tree.Free();
        }
    }

    {
        // height:stretch ignores align-items:flex-start and subtracts the
        // item's vertical margins. flex_item_height_stretch, content-box.
        TaffyTree tree;
        tree.Init();
        taffy::Style plain;
        plain.boxSizing = BoxSizing::ContentBox;
        plain.size.width = Dimension::Length(40.0f);
        plain.size.height = Dimension::Stretch();
        TaffyNodeId a = tree.NewLeaf(plain);
        taffy::Style margined = plain;
        margined.margin.top = LengthPercentageAuto::Length(5.0f);
        margined.margin.bottom = LengthPercentageAuto::Length(10.0f);
        TaffyNodeId b = tree.NewLeaf(margined);

        taffy::Style rootStyle;
        rootStyle.boxSizing = BoxSizing::ContentBox;
        rootStyle.alignItems = AlignItems{AlignItemsKeyword::FlexStart};
        rootStyle.size = SizeDim::FromLengths(120.0f, 60.0f);
        TaffyNodeId kids[2] = {a, b};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 120.0f, 60.0f);
        AssertBox(tree.GetLayout(a), 0.0f, 0.0f, 40.0f, 60.0f);
        AssertBox(tree.GetLayout(b), 40.0f, 5.0f, 40.0f, 45.0f);
        tree.Free();
    }

    {
        // min-content / max-content / fit-content on a flex item's width.
        // flex_item_width_keywords, content-box. The measure stands in for
        // the fixture text (min 20x20, max 40x10).
        TaffyTree tree;
        tree.Init();
        taffy::Style minStyle;
        minStyle.boxSizing = BoxSizing::ContentBox;
        minStyle.size.width = Dimension::MinContent();
        taffy::Style maxStyle = minStyle;
        maxStyle.size.width = Dimension::MaxContent();
        taffy::Style fitStyle = minStyle;
        fitStyle.size.width = Dimension::FitContent();
        TaffyNodeId a = tree.NewLeaf(minStyle);
        TaffyNodeId b = tree.NewLeaf(maxStyle);
        TaffyNodeId c = tree.NewLeaf(fitStyle);
        taffy::Style rootStyle;
        rootStyle.boxSizing = BoxSizing::ContentBox;
        rootStyle.alignItems = AlignItems{AlignItemsKeyword::FlexStart};
        rootStyle.size.width = Dimension::Length(200.0f);
        TaffyNodeId kids[3] = {a, b, c};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 3);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      KeywordTextMeasure, nullptr);
        AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 200.0f, 20.0f);
        AssertBox(tree.GetLayout(a), 0.0f, 0.0f, 20.0f, 20.0f);
        AssertBox(tree.GetLayout(b), 20.0f, 0.0f, 40.0f, 10.0f);
        AssertBox(tree.GetLayout(c), 60.0f, 0.0f, 40.0f, 10.0f);
        tree.Free();
    }

    {
        // An absolute stretch width subtracts insets and margins.
        // flex_absolute_width_stretch_inset_margin, content-box.
        TaffyTree tree;
        tree.Init();
        taffy::Style leftInset;
        leftInset.boxSizing = BoxSizing::ContentBox;
        leftInset.position = taffy::Position::Absolute;
        leftInset.size.width = Dimension::Stretch();
        leftInset.size.height = Dimension::Length(10.0f);
        leftInset.margin.right = LengthPercentageAuto::Length(5.0f);
        leftInset.inset.top = LengthPercentageAuto::Length(0.0f);
        leftInset.inset.left = LengthPercentageAuto::Length(10.0f);
        TaffyNodeId a = tree.NewLeaf(leftInset);
        taffy::Style rightInset;
        rightInset.boxSizing = BoxSizing::ContentBox;
        rightInset.position = taffy::Position::Absolute;
        rightInset.size.width = Dimension::Stretch();
        rightInset.size.height = Dimension::Length(10.0f);
        rightInset.inset.top = LengthPercentageAuto::Length(40.0f);
        rightInset.inset.right = LengthPercentageAuto::Length(20.0f);
        TaffyNodeId b = tree.NewLeaf(rightInset);
        taffy::Style tall;
        tall.boxSizing = BoxSizing::ContentBox;
        tall.position = taffy::Position::Absolute;
        tall.size.width = Dimension::Length(20.0f);
        tall.size.height = Dimension::Stretch();
        tall.inset.top = LengthPercentageAuto::Length(0.0f);
        tall.inset.left = LengthPercentageAuto::Length(10.0f);
        TaffyNodeId c = tree.NewLeaf(tall);
        taffy::Style rootStyle;
        rootStyle.boxSizing = BoxSizing::ContentBox;
        rootStyle.size = SizeDim::FromLengths(120.0f, 120.0f);
        TaffyNodeId kids[3] = {a, b, c};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 3);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertBox(tree.GetLayout(a), 10.0f, 0.0f, 105.0f, 10.0f);
        AssertBox(tree.GetLayout(b), 0.0f, 40.0f, 100.0f, 10.0f);
        AssertBox(tree.GetLayout(c), 10.0f, 0.0f, 20.0f, 120.0f);
        tree.Free();
    }

    {
        // flex-basis keywords and content. flex-shrink stays 0 so a
        // max-content basis can overflow the container. The measure stands
        // in for "XXXX" + ZWSP + "XXXX" (min 40, max 80, height 20).
        struct BasisCase {
            Dimension basis;
            FlexDirection dir;
            float rootW;
            float rootH;
            float x;
            float w;
            float h;
            bool needsMeasure;
        };
        BasisCase cases[] = {
            {Dimension::MinContent(), FlexDirection::Row, 200.0f, 50.0f, 0.0f,
             40.0f, 50.0f, true},
            {Dimension::MinContent(), FlexDirection::Row, 200.0f, 50.0f, 160.0f,
             40.0f, 50.0f, true},
            {Dimension::MaxContent(), FlexDirection::Row, 60.0f, 50.0f, 0.0f,
             80.0f, 50.0f, true},
            {Dimension::FitContent(), FlexDirection::Row, 60.0f, 50.0f, 0.0f,
             60.0f, 50.0f, true},
            {Dimension::Stretch(), FlexDirection::Row, 200.0f, 50.0f, 0.0f,
             200.0f, 50.0f, false},
            {Dimension::Content(), FlexDirection::Row, 200.0f, 50.0f, 0.0f,
             80.0f, 50.0f, true},
            {Dimension::MinContent(), FlexDirection::Column, 50.0f, 200.0f,
             0.0f, 50.0f, 20.0f, true},
            {Dimension::MaxContent(), FlexDirection::Column, 50.0f, 200.0f,
             0.0f, 50.0f, 20.0f, true},
            {Dimension::Stretch(), FlexDirection::Column, 50.0f, 200.0f, 0.0f,
             50.0f, 200.0f, false},
            {Dimension::Content(), FlexDirection::Column, 50.0f, 200.0f, 0.0f,
             50.0f, 20.0f, true},
        };
        Direction textDir[] = {Direction::Ltr, Direction::Rtl, Direction::Ltr,
                               Direction::Ltr, Direction::Ltr, Direction::Ltr,
                               Direction::Ltr, Direction::Ltr, Direction::Ltr,
                               Direction::Ltr};
        for (int i = 0; i < 10; i++) {
            BasisCase c = cases[i];
            TaffyTree tree;
            tree.Init();
            taffy::Style childStyle;
            childStyle.boxSizing = BoxSizing::ContentBox;
            childStyle.direction = textDir[i];
            childStyle.flexShrink = 0.0f;
            childStyle.flexBasis = c.basis;
            // content ignores a competing main size. The other cases leave
            // that size auto.
            if (c.basis.IsContent() && c.dir == FlexDirection::Row) {
                childStyle.size.width = Dimension::Length(30.0f);
            }
            if (c.basis.IsContent() && c.dir == FlexDirection::Column) {
                childStyle.size.height = Dimension::Length(100.0f);
            }
            TaffyNodeId child = tree.NewLeaf(childStyle);
            taffy::Style rootStyle;
            rootStyle.boxSizing = BoxSizing::ContentBox;
            rootStyle.direction = textDir[i];
            rootStyle.flexDirection = c.dir;
            rootStyle.size = SizeDim::FromLengths(c.rootW, c.rootH);
            TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
            if (c.needsMeasure) {
                tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                              ContentTextMeasure, nullptr);
            } else {
                tree.ComputeLayout(root, SizeAvail::MaxContent());
            }
            AssertBox(tree.GetLayout(root), 0.0f, 0.0f, c.rootW, c.rootH);
            AssertBox(tree.GetLayout(child), c.x, 0.0f, c.w, c.h);
            tree.Free();
        }
    }

    {
        // margin_auto_multiple_children_row_justify_center. Auto main
        // margins absorb the free space, so justify-content does not
        // shift the line again.
        TaffyTree tree;
        tree.Init();
        taffy::Style autoRight;
        autoRight.size = SizeDim::FromLengths(50.0f, 50.0f);
        autoRight.margin.right = LengthPercentageAuto::Auto();
        TaffyNodeId a = tree.NewLeaf(autoRight);
        taffy::Style plain;
        plain.size = SizeDim::FromLengths(50.0f, 50.0f);
        TaffyNodeId b = tree.NewLeaf(plain);
        taffy::Style rootStyle;
        rootStyle.justifyContent = AlignContent{AlignContentKeyword::Center};
        rootStyle.size = SizeDim::FromLengths(200.0f, 200.0f);
        TaffyNodeId kids[] = {a, b};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(a).location.x, 0.0f);
        utassertnear(tree.GetLayout(b).location.x, 150.0f);
        tree.Free();

        // margin_auto_left_justify_flex_end. The auto margin already
        // places the item at the end.
        tree.Init();
        taffy::Style autoLeft;
        autoLeft.size = SizeDim::FromLengths(50.0f, 50.0f);
        autoLeft.margin.left = LengthPercentageAuto::Auto();
        TaffyNodeId c = tree.NewLeaf(autoLeft);
        rootStyle.justifyContent = AlignContent{AlignContentKeyword::FlexEnd};
        root = tree.NewWithChildren(rootStyle, &c, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(c).location.x, 150.0f);
        tree.Free();
    }
}

// Negative margins shrink a flex container's intrinsic main size, and a
// zero flex-shrink factor does not divide by a zero scaled factor.
static void TestFlexNegativeMarginIntrinsic() {
    TestSuite("taffy flex negative margin intrinsic");

    TaffyTree tree;
    tree.Init();

    taffy::Style colA;
    colA.flexShrink = 0.0f;
    colA.size = SizeDim::FromLengths(20.0f, 10.0f);
    colA.margin.bottom = LengthPercentageAuto::Length(-0.5f);
    TaffyNodeId colANode = tree.NewLeaf(colA);
    taffy::Style colB;
    colB.flexShrink = 0.0f;
    colB.size = SizeDim::FromLengths(20.0f, 20.0f);
    colB.margin.top = LengthPercentageAuto::Length(-5.0f);
    TaffyNodeId colBNode = tree.NewLeaf(colB);
    TaffyNodeId colKids[] = {colANode, colBNode};
    taffy::Style col;
    col.flexDirection = FlexDirection::Column;
    col.flexShrink = 0.0f;
    col.size.width = Dimension::Length(100.0f);
    TaffyNodeId colNode = tree.NewWithChildren(col, colKids, 2);

    taffy::Style rowA;
    rowA.flexShrink = 0.0f;
    rowA.size = SizeDim::FromLengths(10.0f, 20.0f);
    rowA.margin.right = LengthPercentageAuto::Length(-0.5f);
    TaffyNodeId rowANode = tree.NewLeaf(rowA);
    taffy::Style rowB;
    rowB.flexShrink = 0.0f;
    rowB.size = SizeDim::FromLengths(20.0f, 20.0f);
    rowB.margin.left = LengthPercentageAuto::Length(-5.0f);
    TaffyNodeId rowBNode = tree.NewLeaf(rowB);
    TaffyNodeId rowKids[] = {rowANode, rowBNode};
    taffy::Style row;
    row.flexShrink = 0.0f;
    TaffyNodeId rowNode = tree.NewWithChildren(row, rowKids, 2);

    taffy::Style text;
    text.flexGrow = 1.0f;
    text.flexShrink = 0.0f;
    text.margin.left = LengthPercentageAuto::Length(-5.0f);
    SizeF textSize = {40.0f, 10.0f};
    TaffyNodeId textNode = tree.NewLeafWithContext(text, &textSize);
    taffy::Style textRow;
    textRow.flexShrink = 0.0f;
    TaffyNodeId textRowNode = tree.NewWithChildren(textRow, &textNode, 1);

    taffy::Style wrapA;
    wrapA.size.width = Dimension::Length(100.0f);
    TaffyNodeId wrapANode = tree.NewLeaf(wrapA);
    taffy::Style wrapB;
    wrapB.size.width = Dimension::Length(0.0f);
    wrapB.margin.left = LengthPercentageAuto::Length(-10.0f);
    TaffyNodeId wrapBNode = tree.NewLeaf(wrapB);
    TaffyNodeId wrapKids[] = {wrapANode, wrapBNode};
    taffy::Style wrap;
    wrap.flexWrap = FlexWrap::Wrap;
    wrap.flexShrink = 0.0f;
    wrap.size.width = Dimension::MaxContent();
    TaffyNodeId wrapNode = tree.NewWithChildren(wrap, wrapKids, 2);

    TaffyNodeId kids[] = {colNode, rowNode, textRowNode, wrapNode};
    taffy::Style rootStyle;
    rootStyle.flexDirection = FlexDirection::Column;
    rootStyle.alignItems = AlignItems{AlignItemsKeyword::FlexStart};
    TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 4);
    tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                  SizeMeasureFunction, nullptr);

    AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 100.0f, 55.0f);
    AssertBox(tree.GetLayout(colNode), 0.0f, 0.0f, 100.0f, 25.0f);
    AssertBox(tree.GetLayout(colANode), 0.0f, 0.0f, 20.0f, 10.0f);
    AssertBox(tree.GetLayout(colBNode), 0.0f, 5.0f, 20.0f, 20.0f);
    AssertBox(tree.GetLayout(rowNode), 0.0f, 25.0f, 25.0f, 20.0f);
    AssertBox(tree.GetLayout(rowANode), 0.0f, 0.0f, 10.0f, 20.0f);
    AssertBox(tree.GetLayout(rowBNode), 5.0f, 0.0f, 20.0f, 20.0f);
    AssertBox(tree.GetLayout(textRowNode), 0.0f, 45.0f, 35.0f, 10.0f);
    AssertBox(tree.GetLayout(textNode), -5.0f, 0.0f, 40.0f, 10.0f);
    AssertBox(tree.GetLayout(wrapNode), 0.0f, 55.0f, 100.0f, 0.0f);
    AssertBox(tree.GetLayout(wrapANode), 0.0f, 0.0f, 100.0f, 0.0f);
    AssertBox(tree.GetLayout(wrapBNode), 90.0f, 0.0f, 0.0f, 0.0f);
    tree.Free();
}

// ─── flex-wrap: balance (1bade665) ───────────────────────────────────────

static void TestFlexBalanceCounts() {
    TestSuite("taffy::compute::flexbox (balance)");

    {
        float sizes[3] = {70.0f, 0.0f, 20.0f};
        int out[3];
        int n = taffy::BalancedLineItemCounts(sizes, 3, 100.0f, 10.0f, 1, out);
        utassert(n == 2);
        utassert(out[0] == 2 && out[1] == 1);
    }
    {
        float sizes[3] = {150.0f, 0.0f, 30.0f};
        int out[3];
        int n = taffy::BalancedLineItemCounts(sizes, 3, 100.0f, 0.0f, 1, out);
        utassert(n == 2);
        utassert(out[0] == 1 && out[1] == 2);
    }
    {
        float sizes[3] = {70.0f, 0.0f, 0.0f};
        int out[3];
        int n = taffy::BalancedLineItemCounts(sizes, 3, 100.0f, 0.0f, 2, out);
        utassert(n == 2);
        utassert(out[0] == 2 && out[1] == 1);
    }

    // The rust property test: the divide-and-conquer counts match the naive
    // program on 5000 deterministic inputs.
    uint64_t state = 0x243F6A8885A308D3ULL;
    for (int c = 0; c < 5000; c++) {
        auto next = [&](uint32_t bound) -> uint32_t {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            return (uint32_t)(state >> 32) % bound;
        };
        int itemCount = c % 20 == 0 ? (int)next(60) + 1 : (int)next(15) + 1;
        float itemSizes[60];
        for (int i = 0; i < itemCount; i++) {
            uint32_t kind = next(8);
            if (kind <= 1) {
                itemSizes[i] = 0.0f;
            } else if (kind == 2) {
                itemSizes[i] = -10.0f;
            } else {
                itemSizes[i] = (float)next(8) * 25.0f;
            }
        }
        float lineLimit = next(5) == 0 ? INFINITY : (float)next(10) * 30.0f;
        float gap = (float)next(4) * 5.0f;
        int minLines = (int)next((uint32_t)itemCount + 2);
        int expected[60];
        int actual[60];
        int nExpect = taffy::NaiveBalancedLineItemCounts(
            itemSizes, itemCount, lineLimit, gap, minLines, expected);
        int nActual = taffy::BalancedLineItemCounts(
            itemSizes, itemCount, lineLimit, gap, minLines, actual);
        utassert(nExpect == nActual);
        utassert(nExpect > 0);
        for (int i = 0; i < nExpect; i++) {
            utassert(expected[i] == actual[i]);
        }
    }
}

static void TestFlexBalanceLayout() {
    TestSuite("taffy::compute::flexbox (balance layout)");

    {
        // balance_row, content-box. 31+32 and 33+34.
        TaffyTree tree;
        tree.Init();
        float widths[4] = {31.0f, 32.0f, 33.0f, 34.0f};
        TaffyNodeId kids[4];
        for (int i = 0; i < 4; i++) {
            taffy::Style child;
            child.boxSizing = BoxSizing::ContentBox;
            child.size.width = Dimension::Length(widths[i]);
            child.size.height = Dimension::Length(30.0f);
            kids[i] = tree.NewLeaf(child);
        }
        taffy::Style rootStyle;
        rootStyle.boxSizing = BoxSizing::ContentBox;
        rootStyle.flexWrap = FlexWrap::Balance;
        rootStyle.size.width = Dimension::Length(100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 4);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 100.0f, 60.0f);
        AssertBox(tree.GetLayout(kids[0]), 0.0f, 0.0f, 31.0f, 30.0f);
        AssertBox(tree.GetLayout(kids[1]), 31.0f, 0.0f, 32.0f, 30.0f);
        AssertBox(tree.GetLayout(kids[2]), 0.0f, 30.0f, 33.0f, 30.0f);
        AssertBox(tree.GetLayout(kids[3]), 33.0f, 30.0f, 34.0f, 30.0f);
        tree.Free();
    }

    {
        // balance_row_line_count: at least 3 lines.
        TaffyTree tree;
        tree.Init();
        float widths[4] = {31.0f, 32.0f, 33.0f, 34.0f};
        TaffyNodeId kids[4];
        for (int i = 0; i < 4; i++) {
            taffy::Style child;
            child.boxSizing = BoxSizing::ContentBox;
            child.size.width = Dimension::Length(widths[i]);
            child.size.height = Dimension::Length(30.0f);
            kids[i] = tree.NewLeaf(child);
        }
        taffy::Style rootStyle;
        rootStyle.boxSizing = BoxSizing::ContentBox;
        rootStyle.flexWrap = FlexWrap::Balance;
        rootStyle.flexLineCount = 3;
        rootStyle.size.width = Dimension::Length(100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 4);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 100.0f, 90.0f);
        AssertBox(tree.GetLayout(kids[0]), 0.0f, 0.0f, 31.0f, 30.0f);
        AssertBox(tree.GetLayout(kids[1]), 31.0f, 0.0f, 32.0f, 30.0f);
        AssertBox(tree.GetLayout(kids[2]), 0.0f, 30.0f, 33.0f, 30.0f);
        AssertBox(tree.GetLayout(kids[3]), 0.0f, 60.0f, 34.0f, 30.0f);
        tree.Free();
    }

    {
        // balance_wrap_reverse_row: the same lines, stacked from the end.
        TaffyTree tree;
        tree.Init();
        float widths[4] = {31.0f, 32.0f, 33.0f, 34.0f};
        TaffyNodeId kids[4];
        for (int i = 0; i < 4; i++) {
            taffy::Style child;
            child.boxSizing = BoxSizing::ContentBox;
            child.size.width = Dimension::Length(widths[i]);
            child.size.height = Dimension::Length(30.0f);
            kids[i] = tree.NewLeaf(child);
        }
        taffy::Style rootStyle;
        rootStyle.boxSizing = BoxSizing::ContentBox;
        rootStyle.flexWrap = FlexWrap::BalanceReverse;
        rootStyle.size.width = Dimension::Length(100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 4);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 100.0f, 60.0f);
        AssertBox(tree.GetLayout(kids[0]), 0.0f, 30.0f, 31.0f, 30.0f);
        AssertBox(tree.GetLayout(kids[1]), 31.0f, 30.0f, 32.0f, 30.0f);
        AssertBox(tree.GetLayout(kids[2]), 0.0f, 0.0f, 33.0f, 30.0f);
        AssertBox(tree.GetLayout(kids[3]), 33.0f, 0.0f, 34.0f, 30.0f);
        tree.Free();
    }

    {
        // balance_column.
        TaffyTree tree;
        tree.Init();
        float heights[4] = {31.0f, 32.0f, 33.0f, 34.0f};
        TaffyNodeId kids[4];
        for (int i = 0; i < 4; i++) {
            taffy::Style child;
            child.boxSizing = BoxSizing::ContentBox;
            child.size.width = Dimension::Length(30.0f);
            child.size.height = Dimension::Length(heights[i]);
            kids[i] = tree.NewLeaf(child);
        }
        taffy::Style rootStyle;
        rootStyle.boxSizing = BoxSizing::ContentBox;
        rootStyle.flexDirection = FlexDirection::Column;
        rootStyle.flexWrap = FlexWrap::Balance;
        rootStyle.size.height = Dimension::Length(100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 4);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 60.0f, 100.0f);
        AssertBox(tree.GetLayout(kids[0]), 0.0f, 0.0f, 30.0f, 31.0f);
        AssertBox(tree.GetLayout(kids[1]), 0.0f, 31.0f, 30.0f, 32.0f);
        AssertBox(tree.GetLayout(kids[2]), 30.0f, 0.0f, 30.0f, 33.0f);
        AssertBox(tree.GetLayout(kids[3]), 30.0f, 33.0f, 30.0f, 34.0f);
        tree.Free();
    }

    {
        // balance_row_zero_sized_items. A zero-width item stays on the
        // preceding line.
        TaffyTree tree;
        tree.Init();
        float widths[5] = {40.0f, 0.0f, 40.0f, 0.0f, 40.0f};
        float heights[5] = {30.0f, 10.0f, 30.0f, 10.0f, 30.0f};
        float xs[5] = {0.0f, 40.0f, 40.0f, 80.0f, 0.0f};
        float ys[5] = {0.0f, 0.0f, 0.0f, 0.0f, 30.0f};
        TaffyNodeId kids[5];
        for (int i = 0; i < 5; i++) {
            taffy::Style child;
            child.boxSizing = BoxSizing::ContentBox;
            child.size.width = Dimension::Length(widths[i]);
            child.size.height = Dimension::Length(heights[i]);
            kids[i] = tree.NewLeaf(child);
        }
        taffy::Style rootStyle;
        rootStyle.boxSizing = BoxSizing::ContentBox;
        rootStyle.flexWrap = FlexWrap::Balance;
        rootStyle.size.width = Dimension::Length(100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 5);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 100.0f, 60.0f);
        for (int i = 0; i < 5; i++) {
            AssertBox(tree.GetLayout(kids[i]), xs[i], ys[i], widths[i],
                      heights[i]);
        }
        tree.Free();
    }

    {
        // flex-line-count does not wrap a nowrap container.
        // balance_nowrap_line_count_no_effect.
        TaffyTree tree;
        tree.Init();
        TaffyNodeId kids[6];
        for (int i = 0; i < 6; i++) {
            taffy::Style child;
            child.boxSizing = BoxSizing::ContentBox;
            child.flexShrink = 0.0f;
            child.size = SizeDim::FromLengths(20.0f, 20.0f);
            kids[i] = tree.NewLeaf(child);
        }
        taffy::Style rootStyle;
        rootStyle.boxSizing = BoxSizing::ContentBox;
        rootStyle.flexLineCount = 10000;
        rootStyle.size.width = Dimension::Length(20.0f);
        rootStyle.gap.width = LengthPercentage::Length(20.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 6);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 20.0f, 20.0f);
        for (int i = 0; i < 6; i++) {
            AssertBox(tree.GetLayout(kids[i]), (float)i * 40.0f, 0.0f, 20.0f,
                      20.0f);
        }
        tree.Free();
    }

    {
        // flex-line-count divides the cross size a measured item sees.
        // (210 - 10) / 2 = 100, so the Ahem text wraps to 100x30. nowrap
        // keeps the full 210 and wraps to 210x20.
        FlexWrap wraps[2] = {FlexWrap::Wrap, FlexWrap::NoWrap};
        float expectW[2] = {100.0f, 210.0f};
        float expectH[2] = {30.0f, 20.0f};
        for (int i = 0; i < 2; i++) {
            TaffyTree tree;
            tree.Init();
            taffy::Style childStyle;
            TaffyNodeId child = tree.NewLeaf(childStyle);
            taffy::Style rootStyle;
            rootStyle.flexDirection = FlexDirection::Column;
            rootStyle.flexWrap = wraps[i];
            rootStyle.flexLineCount = 2;
            rootStyle.size = SizeDim::FromLengths(210.0f, 100.0f);
            rootStyle.gap.width = LengthPercentage::Length(10.0f);
            rootStyle.alignItems = AlignItems{AlignItemsKeyword::Start};
            rootStyle.alignContent = AlignContent{AlignContentKeyword::Start};
            TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
            tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                          AhemBalanceMeasure, nullptr);
            AssertBox(tree.GetLayout(child), 0.0f, 0.0f, expectW[i],
                      expectH[i]);
            tree.Free();
        }
    }

    {
        // A bare fit-content cross size uses the same divided stretch size.
        FlexWrap wraps[2] = {FlexWrap::Wrap, FlexWrap::NoWrap};
        float expectW[2] = {100.0f, 210.0f};
        float expectH[2] = {30.0f, 20.0f};
        for (int i = 0; i < 2; i++) {
            TaffyTree tree;
            tree.Init();
            taffy::Style childStyle;
            childStyle.size.width = Dimension::FitContent();
            TaffyNodeId child = tree.NewLeaf(childStyle);
            taffy::Style rootStyle;
            rootStyle.flexDirection = FlexDirection::Column;
            rootStyle.flexWrap = wraps[i];
            rootStyle.flexLineCount = 2;
            rootStyle.size = SizeDim::FromLengths(210.0f, 100.0f);
            rootStyle.gap.width = LengthPercentage::Length(10.0f);
            rootStyle.alignItems = AlignItems{AlignItemsKeyword::Start};
            rootStyle.alignContent = AlignContent{AlignContentKeyword::Start};
            TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
            tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                          AhemBalanceMeasure, nullptr);
            AssertBox(tree.GetLayout(child), 0.0f, 0.0f, expectW[i],
                      expectH[i]);
            tree.Free();
        }
    }
}

// ─── block end-to-end ────────────────────────────────────────────────────

static void TestBlockLayout() {
    TestSuite("taffy::compute::block");

    {
        // Block children stack vertically and stretch to the container width.
        TaffyTree tree;
        tree.Init();
        taffy::Style childStyle;
        childStyle.display = taffy::Display::Block;
        childStyle.size.height = Dimension::Length(20.0f);
        TaffyNodeId c0 = tree.NewLeaf(childStyle);
        TaffyNodeId c1 = tree.NewLeaf(childStyle);
        TaffyNodeId kids[] = {c0, c1};

        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.size = SizeDim::FromLengths(120.0f, 100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        utassertnear(tree.GetLayout(c0).location.y, 0.0f);
        utassertnear(tree.GetLayout(c1).location.y, 20.0f);
        utassertnear(tree.GetLayout(c0).size.w, 120.0f);
        utassertnear(tree.GetLayout(c1).size.w, 120.0f);
        tree.Free();
    }

    {
        // Adjacent vertical margins collapse: 10 and 20 give 20, not 30.
        TaffyTree tree;
        tree.Init();
        taffy::Style childStyle;
        childStyle.display = taffy::Display::Block;
        childStyle.size.height = Dimension::Length(10.0f);
        childStyle.margin.bottom = LengthPercentageAuto::Length(10.0f);
        TaffyNodeId c0 = tree.NewLeaf(childStyle);

        taffy::Style child1Style;
        child1Style.display = taffy::Display::Block;
        child1Style.size.height = Dimension::Length(10.0f);
        child1Style.margin.top = LengthPercentageAuto::Length(20.0f);
        TaffyNodeId c1 = tree.NewLeaf(child1Style);

        TaffyNodeId kids[] = {c0, c1};
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.size.width = Dimension::Length(100.0f);
        // A border stops the container's own margins collapsing with its
        // children's, which is what isolates the pair being tested.
        rootStyle.border = {
            LengthPercentage::Length(1.0f), LengthPercentage::Length(1.0f),
            LengthPercentage::Length(1.0f), LengthPercentage::Length(1.0f)};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        utassertnear(tree.GetLayout(c0).location.y, 1.0f);
        utassertnear(tree.GetLayout(c1).location.y, 31.0f);
        tree.Free();
    }

    {
        // block_margin_y_last_child_collapse_blocked_by_min_height.
        // min-height determines the used height, so the last child's bottom
        // margin stays inside and the parent's own 10px margin is what
        // separates it from the next sibling.
        TaffyTree tree;
        tree.Init();
        taffy::Style innerStyle;
        innerStyle.display = taffy::Display::Block;
        innerStyle.size.height = Dimension::Length(10.0f);
        innerStyle.margin.bottom = LengthPercentageAuto::Length(20.0f);
        TaffyNodeId inner = tree.NewLeaf(innerStyle);

        taffy::Style midStyle;
        midStyle.display = taffy::Display::Block;
        midStyle.minSize.height = Dimension::Length(40.0f);
        midStyle.margin.bottom = LengthPercentageAuto::Length(10.0f);
        TaffyNodeId midKids[] = {inner};
        TaffyNodeId mid = tree.NewWithChildren(midStyle, midKids, 1);

        taffy::Style tailStyle;
        tailStyle.display = taffy::Display::Block;
        tailStyle.size.height = Dimension::Length(10.0f);
        TaffyNodeId tail = tree.NewLeaf(tailStyle);

        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.size.width = Dimension::Length(50.0f);
        TaffyNodeId rootKids[] = {mid, tail};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, rootKids, 2);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        utassertnear(tree.GetLayout(root).size.h, 60.0f);
        utassertnear(tree.GetLayout(mid).location.y, 0.0f);
        utassertnear(tree.GetLayout(mid).size.h, 40.0f);
        utassertnear(tree.GetLayout(inner).location.y, 0.0f);
        utassertnear(tree.GetLayout(inner).size.h, 10.0f);
        utassertnear(tree.GetLayout(tail).location.y, 50.0f);
        utassertnear(tree.GetLayout(tail).size.h, 10.0f);
        tree.Free();
    }

    {
        // block_margin_y_last_child_collapse_not_blocked_by_smaller_min_height.
        // min-height is below the used height, so the last child's 30px
        // margin still collapses with the parent's 10px margin.
        TaffyTree tree;
        tree.Init();
        taffy::Style innerStyle;
        innerStyle.display = taffy::Display::Block;
        innerStyle.size.height = Dimension::Length(20.0f);
        innerStyle.margin.bottom = LengthPercentageAuto::Length(30.0f);
        TaffyNodeId inner = tree.NewLeaf(innerStyle);

        taffy::Style midStyle;
        midStyle.display = taffy::Display::Block;
        midStyle.minSize.height = Dimension::Length(10.0f);
        midStyle.margin.bottom = LengthPercentageAuto::Length(10.0f);
        TaffyNodeId midKids[] = {inner};
        TaffyNodeId mid = tree.NewWithChildren(midStyle, midKids, 1);

        taffy::Style tailStyle;
        tailStyle.display = taffy::Display::Block;
        tailStyle.size.height = Dimension::Length(10.0f);
        TaffyNodeId tail = tree.NewLeaf(tailStyle);

        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.size.width = Dimension::Length(50.0f);
        TaffyNodeId rootKids[] = {mid, tail};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, rootKids, 2);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        utassertnear(tree.GetLayout(root).size.h, 60.0f);
        utassertnear(tree.GetLayout(mid).location.y, 0.0f);
        utassertnear(tree.GetLayout(mid).size.h, 20.0f);
        utassertnear(tree.GetLayout(inner).size.h, 20.0f);
        utassertnear(tree.GetLayout(tail).location.y, 50.0f);
        tree.Free();
    }

    {
        // block_margin_y_collapse_through_child_blocked_by_parent_min_height.
        // An empty child's bottom margin collapses through the top, and
        // min-height keeps it from also collapsing out of the bottom.
        TaffyTree tree;
        tree.Init();
        taffy::Style innerStyle;
        innerStyle.display = taffy::Display::Block;
        innerStyle.margin.bottom = LengthPercentageAuto::Length(20.0f);
        TaffyNodeId inner = tree.NewLeaf(innerStyle);

        taffy::Style midStyle;
        midStyle.display = taffy::Display::Block;
        midStyle.minSize.height = Dimension::Length(30.0f);
        TaffyNodeId midKids[] = {inner};
        TaffyNodeId mid = tree.NewWithChildren(midStyle, midKids, 1);

        taffy::Style tailStyle;
        tailStyle.display = taffy::Display::Block;
        tailStyle.size.height = Dimension::Length(10.0f);
        TaffyNodeId tail = tree.NewLeaf(tailStyle);

        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.size.width = Dimension::Length(50.0f);
        TaffyNodeId rootKids[] = {mid, tail};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, rootKids, 2);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        utassertnear(tree.GetLayout(root).size.h, 60.0f);
        utassertnear(tree.GetLayout(mid).location.y, 20.0f);
        utassertnear(tree.GetLayout(mid).size.h, 30.0f);
        utassertnear(tree.GetLayout(inner).location.y, 0.0f);
        utassertnear(tree.GetLayout(inner).size.h, 0.0f);
        utassertnear(tree.GetLayout(tail).location.y, 50.0f);
        tree.Free();
    }

    {
        // Percentage padding resolves against the containing block's width,
        // not the box's own width. 10% of 200 is 20, not 10% of 50.
        TaffyTree tree;
        tree.Init();
        taffy::Style leafStyle;
        leafStyle.display = taffy::Display::Block;
        leafStyle.size = SizeDim::FromLengths(10.0f, 10.0f);
        TaffyNodeId leaf = tree.NewLeaf(leafStyle);

        taffy::Style midStyle;
        midStyle.display = taffy::Display::Block;
        midStyle.size.width = Dimension::Length(50.0f);
        midStyle.padding.left = LengthPercentage::Percent(0.1f);
        TaffyNodeId midKids[] = {leaf};
        TaffyNodeId mid = tree.NewWithChildren(midStyle, midKids, 1);

        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.size.width = Dimension::Length(200.0f);
        TaffyNodeId rootKids[] = {mid};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, rootKids, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        utassertnear(tree.GetLayout(leaf).location.x, 20.0f);
        tree.Free();
    }

    {
        // align-content shifts a floated child. A 20px float in a 100px
        // flow-root with align-content: flex-end sits at y = 80.
        TaffyTree tree;
        tree.Init();
        taffy::Style floated;
        floated.display = taffy::Display::Block;
        floated.floatMode = taffy::Float::Left;
        floated.size = SizeDim::FromLengths(20.0f, 20.0f);
        TaffyNodeId child = tree.NewLeaf(floated);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::FlowRoot;
        rootStyle.size = SizeDim::FromLengths(100.0f, 100.0f);
        rootStyle.alignContent = AlignContent{AlignContentKeyword::FlexEnd};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(root).size.h, 100.0f);
        utassertnear(tree.GetLayout(child).location.y, 80.0f);
        utassertnear(tree.GetLayout(child).location.x, 0.0f);
        tree.Free();
    }

    {
        // A non-normal align-content establishes a BFC, so a plain block
        // contains its float. A 20x30 left float makes the block 30 tall.
        TaffyTree tree;
        tree.Init();
        taffy::Style floated;
        floated.display = taffy::Display::Block;
        floated.floatMode = taffy::Float::Left;
        floated.size = SizeDim::FromLengths(20.0f, 30.0f);
        TaffyNodeId child = tree.NewLeaf(floated);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.size.width = Dimension::Length(100.0f);
        rootStyle.alignContent = AlignContent{AlignContentKeyword::FlexStart};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(root).size.h, 30.0f);
        utassertnear(tree.GetLayout(child).location.y, 0.0f);
        utassertnear(tree.GetLayout(child).location.x, 0.0f);
        tree.Free();
    }

    {
        // The same BFC stops the block's margin from collapsing with its
        // child's. The child's 20px top margin stays inside the block.
        TaffyTree tree;
        tree.Init();
        taffy::Style innerStyle;
        innerStyle.display = taffy::Display::Block;
        innerStyle.size.height = Dimension::Length(10.0f);
        innerStyle.margin.top = LengthPercentageAuto::Length(20.0f);
        TaffyNodeId inner = tree.NewLeaf(innerStyle);

        taffy::Style midStyle;
        midStyle.display = taffy::Display::Block;
        midStyle.alignContent = AlignContent{AlignContentKeyword::FlexStart};
        TaffyNodeId midKids[] = {inner};
        TaffyNodeId mid = tree.NewWithChildren(midStyle, midKids, 1);

        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.size.width = Dimension::Length(100.0f);
        rootStyle.border = {
            LengthPercentage::Length(1.0f), LengthPercentage::Length(1.0f),
            LengthPercentage::Length(1.0f), LengthPercentage::Length(1.0f)};
        TaffyNodeId rootKids[] = {mid};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, rootKids, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        utassertnear(tree.GetLayout(root).size.h, 32.0f);
        utassertnear(tree.GetLayout(mid).location.y, 1.0f);
        utassertnear(tree.GetLayout(mid).size.h, 30.0f);
        utassertnear(tree.GetLayout(inner).location.y, 20.0f);
        tree.Free();
    }

    {
        // block_absolute_margin_auto_left_and_right_max_width.
        // width 180 clamped by max-width 100 still centres in the 200px
        // containing block: the leftover 100px splits into 50px margins.
        TaffyTree tree;
        tree.Init();
        taffy::Style absStyle;
        absStyle.position = taffy::Position::Absolute;
        absStyle.size = SizeDim::FromLengths(180.0f, 40.0f);
        absStyle.maxSize.width = Dimension::Length(100.0f);
        absStyle.margin = RectLpa::Auto();
        absStyle.inset = {LengthPercentageAuto::Length(0.0f),
                          LengthPercentageAuto::Length(0.0f),
                          LengthPercentageAuto::Length(20.0f),
                          LengthPercentageAuto::Auto()};
        TaffyNodeId child = tree.NewLeaf(absStyle);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.size = SizeDim::FromLengths(200.0f, 100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(child).location.x, 50.0f);
        utassertnear(tree.GetLayout(child).location.y, 20.0f);
        utassertnear(tree.GetLayout(child).size.w, 100.0f);
        utassertnear(tree.GetLayout(child).size.h, 40.0f);
        tree.Free();
    }

    {
        // block_absolute_margin_auto_top_and_bottom_max_height.
        // height 90 clamped by max-height 40 centres vertically: y = 30.
        TaffyTree tree;
        tree.Init();
        taffy::Style absStyle;
        absStyle.position = taffy::Position::Absolute;
        absStyle.size = SizeDim::FromLengths(100.0f, 90.0f);
        absStyle.maxSize.height = Dimension::Length(40.0f);
        absStyle.margin = RectLpa::Auto();
        absStyle.inset = {LengthPercentageAuto::Length(20.0f),
                          LengthPercentageAuto::Auto(),
                          LengthPercentageAuto::Length(0.0f),
                          LengthPercentageAuto::Length(0.0f)};
        TaffyNodeId child = tree.NewLeaf(absStyle);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.size = SizeDim::FromLengths(200.0f, 100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(child).location.x, 20.0f);
        utassertnear(tree.GetLayout(child).location.y, 30.0f);
        utassertnear(tree.GetLayout(child).size.w, 100.0f);
        utassertnear(tree.GetLayout(child).size.h, 40.0f);
        tree.Free();
    }

    {
        // height:stretch fills the block after vertical margins.
        // block_height_stretch, content-box.
        Direction directions[2] = {Direction::Ltr, Direction::Rtl};
        for (int i = 0; i < 2; i++) {
            TaffyTree tree;
            tree.Init();
            taffy::Style childStyle;
            childStyle.boxSizing = BoxSizing::ContentBox;
            childStyle.direction = directions[i];
            childStyle.size.height = Dimension::Stretch();
            childStyle.margin.top = LengthPercentageAuto::Length(5.0f);
            childStyle.margin.bottom = LengthPercentageAuto::Length(10.0f);
            TaffyNodeId child = tree.NewLeaf(childStyle);
            taffy::Style rootStyle;
            rootStyle.display = taffy::Display::Block;
            rootStyle.boxSizing = BoxSizing::ContentBox;
            rootStyle.direction = directions[i];
            rootStyle.size = SizeDim::FromLengths(100.0f, 80.0f);
            TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
            tree.ComputeLayout(root, SizeAvail::MaxContent());
            AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 100.0f, 80.0f);
            AssertBox(tree.GetLayout(child), 0.0f, 5.0f, 100.0f, 65.0f);
            tree.Free();
        }
    }

    {
        // A left float with width:stretch is the container's inner width.
        TaffyTree tree;
        tree.Init();
        taffy::Style floated;
        floated.display = taffy::Display::Block;
        floated.boxSizing = BoxSizing::ContentBox;
        floated.floatMode = taffy::Float::Left;
        floated.size.width = Dimension::Stretch();
        floated.size.height = Dimension::Length(10.0f);
        TaffyNodeId child = tree.NewLeaf(floated);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.boxSizing = BoxSizing::ContentBox;
        rootStyle.size.width = Dimension::Length(100.0f);
        rootStyle.alignContent = AlignContent{AlignContentKeyword::FlexStart};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertBox(tree.GetLayout(child), 0.0f, 0.0f, 100.0f, 10.0f);
        utassertnear(tree.GetLayout(root).size.h, 10.0f);
        tree.Free();
    }

    {
        // block_width_keywords, content-box. Stretch is the container width.
        // fit-content clamps the max-content size to that width. min-content
        // wraps. RTL packs the shorter boxes against the right edge.
        Direction directions[2] = {Direction::Ltr, Direction::Rtl};
        float fitX[2] = {0.0f, 80.0f};
        float minX[2] = {0.0f, 100.0f};
        float maxX[2] = {0.0f, 80.0f};
        for (int i = 0; i < 2; i++) {
            TaffyTree tree;
            tree.Init();
            taffy::Style stretchStyle;
            stretchStyle.boxSizing = BoxSizing::ContentBox;
            stretchStyle.direction = directions[i];
            stretchStyle.size.width = Dimension::Stretch();
            taffy::Style fitStyle = stretchStyle;
            fitStyle.size.width = Dimension::FitContent();
            taffy::Style minStyle = stretchStyle;
            minStyle.size.width = Dimension::MinContent();
            taffy::Style maxStyle = stretchStyle;
            maxStyle.size.width = Dimension::MaxContent();
            TaffyNodeId a = tree.NewLeaf(stretchStyle);
            TaffyNodeId b = tree.NewLeaf(fitStyle);
            TaffyNodeId c = tree.NewLeaf(minStyle);
            TaffyNodeId d = tree.NewLeaf(maxStyle);
            taffy::Style rootStyle;
            rootStyle.display = taffy::Display::Block;
            rootStyle.boxSizing = BoxSizing::ContentBox;
            rootStyle.direction = directions[i];
            rootStyle.size.width = Dimension::Length(120.0f);
            TaffyNodeId kids[4] = {a, b, c, d};
            TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 4);
            tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                          KeywordTextMeasure, nullptr);
            AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 120.0f, 50.0f);
            AssertBox(tree.GetLayout(a), 0.0f, 0.0f, 120.0f, 10.0f);
            AssertBox(tree.GetLayout(b), fitX[i], 10.0f, 40.0f, 10.0f);
            AssertBox(tree.GetLayout(c), minX[i], 20.0f, 20.0f, 20.0f);
            AssertBox(tree.GetLayout(d), maxX[i], 40.0f, 40.0f, 10.0f);
            tree.Free();
        }
    }

    {
        // fit-content(30px) limits the max-content size. fit-content(50%)
        // resolves against the 120px container and does not shrink the
        // 40px max-content size.
        TaffyTree tree;
        tree.Init();
        taffy::Style pxStyle;
        pxStyle.boxSizing = BoxSizing::ContentBox;
        pxStyle.size.width = Dimension::FitContentPx(30.0f);
        taffy::Style pctStyle;
        pctStyle.boxSizing = BoxSizing::ContentBox;
        pctStyle.size.width = Dimension::FitContentPercent(0.5f);
        TaffyNodeId a = tree.NewLeaf(pxStyle);
        TaffyNodeId b = tree.NewLeaf(pctStyle);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.boxSizing = BoxSizing::ContentBox;
        rootStyle.size.width = Dimension::Length(120.0f);
        TaffyNodeId kids[2] = {a, b};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      KeywordTextMeasure, nullptr);
        AssertBox(tree.GetLayout(a), 0.0f, 0.0f, 30.0f, 20.0f);
        AssertBox(tree.GetLayout(b), 0.0f, 20.0f, 40.0f, 10.0f);
        tree.Free();
    }

    {
        // block_absolute_width_stretch_inset_margin, content-box.
        TaffyTree tree;
        tree.Init();
        taffy::Style leftInset;
        leftInset.boxSizing = BoxSizing::ContentBox;
        leftInset.position = taffy::Position::Absolute;
        leftInset.size.width = Dimension::Stretch();
        leftInset.size.height = Dimension::Length(10.0f);
        leftInset.margin.right = LengthPercentageAuto::Length(5.0f);
        leftInset.inset.top = LengthPercentageAuto::Length(0.0f);
        leftInset.inset.left = LengthPercentageAuto::Length(10.0f);
        TaffyNodeId a = tree.NewLeaf(leftInset);
        taffy::Style rightInset;
        rightInset.boxSizing = BoxSizing::ContentBox;
        rightInset.position = taffy::Position::Absolute;
        rightInset.size.width = Dimension::Stretch();
        rightInset.size.height = Dimension::Length(10.0f);
        rightInset.inset.top = LengthPercentageAuto::Length(40.0f);
        rightInset.inset.right = LengthPercentageAuto::Length(20.0f);
        TaffyNodeId b = tree.NewLeaf(rightInset);
        taffy::Style tall;
        tall.boxSizing = BoxSizing::ContentBox;
        tall.position = taffy::Position::Absolute;
        tall.size.width = Dimension::Length(20.0f);
        tall.size.height = Dimension::Stretch();
        tall.inset.top = LengthPercentageAuto::Length(0.0f);
        tall.inset.left = LengthPercentageAuto::Length(10.0f);
        TaffyNodeId c = tree.NewLeaf(tall);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.boxSizing = BoxSizing::ContentBox;
        rootStyle.size = SizeDim::FromLengths(120.0f, 120.0f);
        TaffyNodeId kids[3] = {a, b, c};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 3);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertBox(tree.GetLayout(a), 10.0f, 0.0f, 105.0f, 10.0f);
        AssertBox(tree.GetLayout(b), 0.0f, 40.0f, 100.0f, 10.0f);
        AssertBox(tree.GetLayout(c), 10.0f, 0.0f, 20.0f, 120.0f);
        tree.Free();
    }

    {
        // width:content is not a block sizing keyword. It resolves as auto,
        // so the child stretches to the container.
        TaffyTree tree;
        tree.Init();
        taffy::Style childStyle;
        childStyle.display = taffy::Display::Block;
        childStyle.boxSizing = BoxSizing::ContentBox;
        childStyle.size.width = Dimension::Content();
        childStyle.size.height = Dimension::Length(10.0f);
        TaffyNodeId child = tree.NewLeaf(childStyle);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.boxSizing = BoxSizing::ContentBox;
        rootStyle.size.width = Dimension::Length(100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertBox(tree.GetLayout(child), 0.0f, 0.0f, 100.0f, 10.0f);
        tree.Free();
    }
}

// A cleared float sits below the floats it clears, so a shrink-to-fit
// float container takes the widest uncleared run rather than the sum.
static void TestFloatClearIntrinsicWidth() {
    TestSuite("taffy float clear intrinsic width");

    TaffyTree tree;
    tree.Init();
    taffy::Style rightA;
    rightA.display = taffy::Display::Block;
    rightA.floatMode = taffy::Float::Right;
    rightA.size = SizeDim::FromLengths(100.0f, 20.0f);
    TaffyNodeId rightANode = tree.NewLeaf(rightA);
    taffy::Style rightB;
    rightB.display = taffy::Display::Block;
    rightB.floatMode = taffy::Float::Right;
    rightB.clear = taffy::Clear::Right;
    rightB.size = SizeDim::FromLengths(120.0f, 20.0f);
    TaffyNodeId rightBNode = tree.NewLeaf(rightB);
    TaffyNodeId rightKids[] = {rightANode, rightBNode};
    taffy::Style rightBox;
    rightBox.display = taffy::Display::Block;
    rightBox.floatMode = taffy::Float::Right;
    TaffyNodeId rightNode = tree.NewWithChildren(rightBox, rightKids, 2);

    taffy::Style leftA;
    leftA.display = taffy::Display::Block;
    leftA.floatMode = taffy::Float::Left;
    leftA.size = SizeDim::FromLengths(50.0f, 20.0f);
    TaffyNodeId leftANode = tree.NewLeaf(leftA);
    taffy::Style leftB;
    leftB.display = taffy::Display::Block;
    leftB.floatMode = taffy::Float::Left;
    leftB.clear = taffy::Clear::Left;
    leftB.size = SizeDim::FromLengths(60.0f, 20.0f);
    TaffyNodeId leftBNode = tree.NewLeaf(leftB);
    taffy::Style leftC;
    leftC.display = taffy::Display::Block;
    leftC.floatMode = taffy::Float::Left;
    leftC.size = SizeDim::FromLengths(30.0f, 20.0f);
    TaffyNodeId leftCNode = tree.NewLeaf(leftC);
    TaffyNodeId leftKids[] = {leftANode, leftBNode, leftCNode};
    taffy::Style leftBox;
    leftBox.display = taffy::Display::Block;
    leftBox.floatMode = taffy::Float::Left;
    TaffyNodeId leftNode = tree.NewWithChildren(leftBox, leftKids, 3);

    TaffyNodeId kids[] = {rightNode, leftNode};
    taffy::Style rootStyle;
    rootStyle.display = taffy::Display::Block;
    rootStyle.size = SizeDim::FromLengths(400.0f, 200.0f);
    TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
    tree.ComputeLayout(root, SizeAvail::MaxContent());
    AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 400.0f, 200.0f);
    AssertBox(tree.GetLayout(rightNode), 280.0f, 0.0f, 120.0f, 40.0f);
    AssertBox(tree.GetLayout(rightANode), 20.0f, 0.0f, 100.0f, 20.0f);
    AssertBox(tree.GetLayout(rightBNode), 0.0f, 20.0f, 120.0f, 20.0f);
    AssertBox(tree.GetLayout(leftNode), 0.0f, 0.0f, 90.0f, 40.0f);
    AssertBox(tree.GetLayout(leftANode), 0.0f, 0.0f, 50.0f, 20.0f);
    AssertBox(tree.GetLayout(leftBNode), 0.0f, 20.0f, 60.0f, 20.0f);
    AssertBox(tree.GetLayout(leftCNode), 60.0f, 20.0f, 30.0f, 20.0f);
    tree.Free();
}

// A scroll-container child synthesizes or clamps its baseline to the
// border box. A visible child with no baseline contributes none, so the
// flex parent uses the block's own bottom edge.
static void TestBlockScrollBaselines() {
    TestSuite("taffy block scroll baselines");

    auto flexRoot = [](TaffyTree& tree, TaffyNodeId a, TaffyNodeId b) {
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Flex;
        rootStyle.flexDirection = FlexDirection::Row;
        rootStyle.alignItems = AlignItems{AlignItemsKeyword::Baseline};
        rootStyle.size = SizeDim::FromLengths(200.0f, 120.0f);
        TaffyNodeId kids[] = {a, b};
        return tree.NewWithChildren(rootStyle, kids, 2);
    };
    auto blockWrap = [](TaffyTree& tree, TaffyNodeId child) {
        taffy::Style wrap;
        wrap.display = taffy::Display::Block;
        wrap.size.width = Dimension::Length(40.0f);
        wrap.padding.bottom = LengthPercentage::Length(20.0f);
        return tree.NewWithChildren(wrap, &child, 1);
    };

    {
        TaffyTree tree;
        tree.Init();
        BaselineContext none = {{30.0f, 30.0f}, None()};
        taffy::Style scrollStyle;
        scrollStyle.display = taffy::Display::Block;
        scrollStyle
            .overflow = {taffy::Overflow::Hidden, taffy::Overflow::Hidden};
        scrollStyle.size = SizeDim::FromLengths(30.0f, 30.0f);
        TaffyNodeId scroll = tree.NewLeafWithContext(scrollStyle, &none);
        TaffyNodeId itemA = blockWrap(tree, scroll);
        BaselineContext refCtx = {{40.0f, 80.0f}, Some(10.0f)};
        TaffyNodeId itemB = tree.NewLeafWithContext(taffy::Style{}, &refCtx);
        TaffyNodeId root = flexRoot(tree, itemA, itemB);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      BaselineMeasure, nullptr);
        utassertnear(tree.GetLayout(itemA).location.y, 0.0f);
        utassertnear(tree.GetLayout(itemB).location.y, 20.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        BaselineContext none = {{30.0f, 30.0f}, None()};
        taffy::Style plainStyle;
        plainStyle.display = taffy::Display::Block;
        plainStyle.size = SizeDim::FromLengths(30.0f, 30.0f);
        TaffyNodeId plain = tree.NewLeafWithContext(plainStyle, &none);
        TaffyNodeId itemA = blockWrap(tree, plain);
        BaselineContext refCtx = {{40.0f, 80.0f}, Some(10.0f)};
        TaffyNodeId itemB = tree.NewLeafWithContext(taffy::Style{}, &refCtx);
        TaffyNodeId root = flexRoot(tree, itemA, itemB);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      BaselineMeasure, nullptr);
        utassertnear(tree.GetLayout(itemA).location.y, 0.0f);
        utassertnear(tree.GetLayout(itemB).location.y, 40.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        BaselineContext contentCtx = {{30.0f, 60.0f}, Some(48.0f)};
        taffy::Style contentStyle;
        contentStyle.display = taffy::Display::Block;
        TaffyNodeId content =
            tree.NewLeafWithContext(contentStyle, &contentCtx);
        taffy::Style scrollStyle;
        scrollStyle.display = taffy::Display::Block;
        scrollStyle
            .overflow = {taffy::Overflow::Hidden, taffy::Overflow::Hidden};
        scrollStyle.size = SizeDim::FromLengths(30.0f, 30.0f);
        TaffyNodeId scroll = tree.NewWithChildren(scrollStyle, &content, 1);
        TaffyNodeId itemA = blockWrap(tree, scroll);
        BaselineContext refCtx = {{40.0f, 80.0f}, Some(10.0f)};
        TaffyNodeId itemB = tree.NewLeafWithContext(taffy::Style{}, &refCtx);
        TaffyNodeId root = flexRoot(tree, itemA, itemB);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      BaselineMeasure, nullptr);
        utassertnear(tree.GetLayout(itemA).location.y, 0.0f);
        utassertnear(tree.GetLayout(itemB).location.y, 20.0f);
        tree.Free();
    }
}

// Three left floats whose percentage widths and margins sum to the
// container. f32 rounding must not push the last one onto the next line.
static void TestFloatPercentSumFits() {
    TestSuite("taffy float percentage sum fits");

    TaffyTree tree;
    tree.Init();
    const float kContainer = 1628.0f;
    TaffyNodeId kids[3];
    for (int i = 0; i < 3; i++) {
        taffy::Style child;
        child.display = taffy::Display::Block;
        child.floatMode = Float::Left;
        child.size.width = Dimension::Percent(0.30f);
        child.size.height = Dimension::Length(50.0f);
        if (i > 0) {
            child.margin.left = LengthPercentageAuto::Percent(0.05f);
        }
        kids[i] = tree.NewLeaf(child);
    }
    taffy::Style rootStyle;
    rootStyle.display = taffy::Display::Block;
    rootStyle.size.width = Dimension::Length(kContainer);
    TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 3);
    tree.ComputeLayout(root, SizeAvail::MaxContent());
    // Rounding is on, so 30% of 1628 (488.4) and the 5% margins land on
    // the integer boxes the fixture records. The last float stays at y 0.
    utassertnear(tree.GetLayout(root).size.w, 1628.0f);
    utassertnear(tree.GetLayout(root).size.h, 50.0f);
    const float kX[3] = {0.0f, 570.0f, 1140.0f};
    for (int i = 0; i < 3; i++) {
        Layout box = tree.GetLayout(kids[i]);
        utassertnear(box.location.x, kX[i]);
        utassertnear(box.location.y, 0.0f);
        utassertnear(box.size.w, 488.0f);
        utassertnear(box.size.h, 50.0f);
    }
    tree.Free();
}

// taffy d53d0200. A float-avoiding block's used auto margin includes the
// gap the float occupied. A relative inset and clearance do not.
static void TestFloatAvoidingAutoMargins() {
    TestSuite("taffy float avoiding auto margins");
    taffy::Direction directions[2] = {taffy::Direction::Ltr,
                                      taffy::Direction::Rtl};
    float pairs[4][2] = {
        {0.0f, 20.0f}, {20.0f, 0.0f}, {20.0f, 20.0f}, {0.0f, 0.0f}};
    bool autoSides[3][2] = {{true, true}, {true, false}, {false, true}};
    for (taffy::Direction direction : directions) {
        for (int p = 0; p < 4; p++) {
            float leftFloatWidth = pairs[p][0];
            float rightFloatWidth = pairs[p][1];
            TaffyTree tree;
            tree.Init();
            taffy::Style leftFloatStyle;
            leftFloatStyle.display = taffy::Display::Block;
            leftFloatStyle.floatMode = taffy::Float::Left;
            leftFloatStyle.size = SizeDim::FromLengths(leftFloatWidth, 40.0f);
            TaffyNodeId leftFloat = tree.NewLeaf(leftFloatStyle);
            taffy::Style rightFloatStyle;
            rightFloatStyle.display = taffy::Display::Block;
            rightFloatStyle.floatMode = taffy::Float::Right;
            rightFloatStyle.size = SizeDim::FromLengths(rightFloatWidth, 40.0f);
            TaffyNodeId rightFloat = tree.NewLeaf(rightFloatStyle);
            TaffyNodeId boxes[3];
            for (int i = 0; i < 3; i++) {
                taffy::Style box;
                box.display = taffy::Display::FlowRoot;
                box.direction = direction;
                box.size = SizeDim::FromLengths(40.0f, 10.0f);
                box.margin.left = autoSides[i][0]
                                      ? LengthPercentageAuto::Auto()
                                      : LengthPercentageAuto::Zero();
                box.margin.right = autoSides[i][1]
                                       ? LengthPercentageAuto::Auto()
                                       : LengthPercentageAuto::Zero();
                box.margin.top = LengthPercentageAuto::Auto();
                box.margin.bottom = LengthPercentageAuto::Auto();
                boxes[i] = tree.NewLeaf(box);
            }
            TaffyNodeId kids[5] = {leftFloat, rightFloat, boxes[0], boxes[1],
                                   boxes[2]};
            taffy::Style root;
            root.display = taffy::Display::FlowRoot;
            root.direction = direction;
            root.size.width = taffy::Dimension::Length(100.0f);
            root.padding.left = taffy::LengthPercentage::Length(5.0f);
            root.padding.right = taffy::LengthPercentage::Length(5.0f);
            root.padding.top = taffy::LengthPercentage::Length(5.0f);
            root.padding.bottom = taffy::LengthPercentage::Length(5.0f);
            TaffyNodeId node = tree.NewWithChildren(root, kids, 5);
            tree.ComputeLayout(node, SizeAvail::MaxContent());
            float freeSpace = 90.0f - leftFloatWidth - rightFloatWidth - 40.0f;
            for (int i = 0; i < 3; i++) {
                bool leftAuto = autoSides[i][0];
                bool rightAuto = autoSides[i][1];
                int autoCount = (leftAuto ? 1 : 0) + (rightAuto ? 1 : 0);
                float alignment = freeSpace / (float)autoCount;
                float leftGap = leftAuto ? alignment : 0.0f;
                Layout box = tree.GetLayout(boxes[i]);
                utassertnear(box.location.x, 5.0f + leftFloatWidth + leftGap);
                utassertnear(box.location.y, 5.0f + (float)i * 10.0f);
                utassertnear(box.size.w, 40.0f);
                utassertnear(box.size.h, 10.0f);
                utassertnear(box.margin.left,
                             leftAuto ? leftFloatWidth + alignment : 0.0f);
                utassertnear(box.margin.right,
                             rightAuto ? rightFloatWidth + alignment : 0.0f);
                utassertnear(box.margin.top, 0.0f);
                utassertnear(box.margin.bottom, 0.0f);
            }
            tree.Free();
        }
    }

    for (taffy::Direction direction : directions) {
        TaffyTree tree;
        tree.Init();
        taffy::Style leftFloatStyle;
        leftFloatStyle.display = taffy::Display::Block;
        leftFloatStyle.floatMode = taffy::Float::Left;
        leftFloatStyle.size = SizeDim::FromLengths(40.0f, 40.0f);
        TaffyNodeId leftFloat = tree.NewLeaf(leftFloatStyle);
        taffy::Style rightFloatStyle;
        rightFloatStyle.display = taffy::Display::Block;
        rightFloatStyle.floatMode = taffy::Float::Right;
        rightFloatStyle.size = SizeDim::FromLengths(60.0f, 40.0f);
        TaffyNodeId rightFloat = tree.NewLeaf(rightFloatStyle);
        taffy::Style boxStyle;
        boxStyle.display = taffy::Display::FlowRoot;
        boxStyle.direction = direction;
        boxStyle.size = SizeDim::FromLengths(40.0f, 10.0f);
        boxStyle.margin.left = LengthPercentageAuto::Auto();
        boxStyle.margin.right = LengthPercentageAuto::Auto();
        boxStyle.margin.top = LengthPercentageAuto::Zero();
        boxStyle.margin.bottom = LengthPercentageAuto::Zero();
        taffy::Style relativeStyle = boxStyle;
        relativeStyle.position = taffy::Position::Relative;
        relativeStyle.inset.left = LengthPercentageAuto::Length(7.0f);
        TaffyNodeId relativeBox = tree.NewLeaf(relativeStyle);
        taffy::Style clearedStyle = boxStyle;
        clearedStyle.clear = taffy::Clear::Both;
        TaffyNodeId clearedBox = tree.NewLeaf(clearedStyle);
        TaffyNodeId nestedKids[2] = {relativeBox, clearedBox};
        taffy::Style nestedStyle;
        nestedStyle.display = taffy::Display::Block;
        nestedStyle.direction = direction;
        nestedStyle.margin.left = LengthPercentageAuto::Length(20.0f);
        nestedStyle.margin.right = LengthPercentageAuto::Length(30.0f);
        nestedStyle.padding.left = taffy::LengthPercentage::Length(5.0f);
        nestedStyle.padding.right = taffy::LengthPercentage::Length(7.0f);
        nestedStyle.border.left = taffy::LengthPercentage::Length(3.0f);
        nestedStyle.border.right = taffy::LengthPercentage::Length(5.0f);
        TaffyNodeId nested = tree.NewWithChildren(nestedStyle, nestedKids, 2);
        TaffyNodeId rootKids[3] = {leftFloat, rightFloat, nested};
        taffy::Style root;
        root.display = taffy::Display::FlowRoot;
        root.direction = direction;
        root.size.width = taffy::Dimension::Length(200.0f);
        root.padding.left = taffy::LengthPercentage::Length(10.0f);
        root.padding.right = taffy::LengthPercentage::Length(10.0f);
        root.padding.top = taffy::LengthPercentage::Length(10.0f);
        root.padding.bottom = taffy::LengthPercentage::Length(10.0f);
        TaffyNodeId node = tree.NewWithChildren(root, rootKids, 3);
        tree.ComputeLayout(node, SizeAvail::MaxContent());
        Layout relative = tree.GetLayout(relativeBox);
        utassertnear(relative.location.x, 47.0f);
        utassertnear(relative.location.y, 0.0f);
        utassertnear(relative.margin.left, 32.0f);
        utassertnear(relative.margin.right, 38.0f);
        Layout cleared = tree.GetLayout(clearedBox);
        utassertnear(cleared.location.x, 43.0f);
        utassertnear(cleared.location.y, 40.0f);
        utassertnear(cleared.margin.left, 35.0f);
        utassertnear(cleared.margin.right, 35.0f);
        Layout nestedLayout = tree.GetLayout(nested);
        utassertnear(nestedLayout.location.x, 30.0f);
        utassertnear(nestedLayout.location.y, 10.0f);
        utassertnear(nestedLayout.margin.left, 20.0f);
        utassertnear(nestedLayout.margin.right, 30.0f);
        tree.Free();
    }
}

// taffy 0a529179. An in-flow block honors justify-self: an auto width
// is fit-content, the margin box is aligned, and that wins over
// text-align. Auto margins still win. stretch also stretches a replaced
// box. safe falls back to start when the box overflows.
static void TestBlockJustifySelf() {
    TestSuite("taffy block justify-self");
    auto leaf = [](float w, float h, taffy::AlignItemsKeyword keyword) {
        taffy::Style s;
        s.display = taffy::Display::Block;
        s.size = SizeDim::FromLengths(w, h);
        s.justifySelf = taffy::OptAlignSelf(taffy::AlignItems{keyword});
        return s;
    };
    {
        TaffyTree tree;
        tree.Init();
        TaffyNodeId kids[3] = {
            tree.NewLeaf(leaf(50.0f, 20.0f, taffy::AlignItemsKeyword::Center)),
            tree.NewLeaf(leaf(50.0f, 20.0f, taffy::AlignItemsKeyword::End)),
            tree.NewLeaf(leaf(50.0f, 20.0f, taffy::AlignItemsKeyword::Start))};
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.size = SizeDim::FromLengths(200.0f, 100.0f);
        TaffyNodeId node = tree.NewWithChildren(root, kids, 3);
        tree.ComputeLayout(node, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(kids[0]).location.x, 75.0f);
        utassertnear(tree.GetLayout(kids[1]).location.x, 150.0f);
        utassertnear(tree.GetLayout(kids[2]).location.x, 0.0f);
        utassertnear(tree.GetLayout(kids[0]).location.y, 0.0f);
        utassertnear(tree.GetLayout(kids[1]).location.y, 20.0f);
        utassertnear(tree.GetLayout(kids[2]).location.y, 40.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style end = leaf(50.0f, 20.0f, taffy::AlignItemsKeyword::End);
        TaffyNodeId child = tree.NewLeaf(end);
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.direction = taffy::Direction::Rtl;
        root.size = SizeDim::FromLengths(200.0f, 100.0f);
        TaffyNodeId node = tree.NewWithChildren(root, &child, 1);
        tree.ComputeLayout(node, SizeAvail::MaxContent());
        // Inline end in RTL is the physical left.
        utassertnear(tree.GetLayout(child).location.x, 0.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::AlignItems safeCenter{taffy::AlignItemsKeyword::Center,
                                     taffy::AlignmentSafety::Safe};
        taffy::AlignItems unsafeCenter{taffy::AlignItemsKeyword::Center,
                                       taffy::AlignmentSafety::Unsafe};
        taffy::AlignItems safeEnd{taffy::AlignItemsKeyword::End,
                                  taffy::AlignmentSafety::Safe};
        taffy::AlignItems plainEnd{taffy::AlignItemsKeyword::End};
        taffy::Style styles[4];
        taffy::AlignItems aligns[4] = {safeCenter, unsafeCenter, safeEnd,
                                       plainEnd};
        TaffyNodeId kids[4];
        for (int i = 0; i < 4; i++) {
            styles[i].display = taffy::Display::Block;
            styles[i].size = SizeDim::FromLengths(150.0f, 20.0f);
            styles[i].justifySelf = taffy::OptAlignSelf(aligns[i]);
            kids[i] = tree.NewLeaf(styles[i]);
        }
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.size = SizeDim::FromLengths(100.0f, 100.0f);
        TaffyNodeId node = tree.NewWithChildren(root, kids, 4);
        tree.ComputeLayout(node, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(kids[0]).location.x, 0.0f);
        utassertnear(tree.GetLayout(kids[1]).location.x, -25.0f);
        utassertnear(tree.GetLayout(kids[2]).location.x, 0.0f);
        utassertnear(tree.GetLayout(kids[3]).location.x, -50.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style selfEnd;
        selfEnd.display = taffy::Display::Block;
        selfEnd.size = SizeDim::FromLengths(50.0f, 20.0f);
        selfEnd.justifySelf = taffy::OptAlignSelf(
            taffy::AlignItems{taffy::AlignItemsKeyword::SelfEnd});
        TaffyNodeId endNode = tree.NewLeaf(selfEnd);
        taffy::Style selfStart = selfEnd;
        selfStart.direction = taffy::Direction::Rtl;
        selfStart.justifySelf = taffy::OptAlignSelf(
            taffy::AlignItems{taffy::AlignItemsKeyword::SelfStart});
        TaffyNodeId startNode = tree.NewLeaf(selfStart);
        TaffyNodeId kids[2] = {endNode, startNode};
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.size = SizeDim::FromLengths(200.0f, 100.0f);
        TaffyNodeId node = tree.NewWithChildren(root, kids, 2);
        tree.ComputeLayout(node, SizeAvail::MaxContent());
        // self-end matches the container. self-start on an RTL item in an
        // LTR container flips to end.
        utassertnear(tree.GetLayout(endNode).location.x, 150.0f);
        utassertnear(tree.GetLayout(startNode).location.x, 150.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style endAuto =
            leaf(50.0f, 20.0f, taffy::AlignItemsKeyword::End);
        endAuto.margin.right = LengthPercentageAuto::Auto();
        taffy::Style startAuto =
            leaf(50.0f, 20.0f, taffy::AlignItemsKeyword::Start);
        startAuto.margin.left = LengthPercentageAuto::Auto();
        taffy::Style bothAuto = startAuto;
        bothAuto.margin.right = LengthPercentageAuto::Auto();
        TaffyNodeId kids[3] = {tree.NewLeaf(endAuto), tree.NewLeaf(startAuto),
                               tree.NewLeaf(bothAuto)};
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.size = SizeDim::FromLengths(200.0f, 100.0f);
        TaffyNodeId node = tree.NewWithChildren(root, kids, 3);
        tree.ComputeLayout(node, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(kids[0]).location.x, 0.0f);
        utassertnear(tree.GetLayout(kids[0]).margin.right, 150.0f);
        utassertnear(tree.GetLayout(kids[1]).location.x, 150.0f);
        utassertnear(tree.GetLayout(kids[1]).margin.left, 150.0f);
        utassertnear(tree.GetLayout(kids[2]).location.x, 75.0f);
        utassertnear(tree.GetLayout(kids[2]).margin.left, 75.0f);
        utassertnear(tree.GetLayout(kids[2]).margin.right, 75.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style start =
            leaf(50.0f, 20.0f, taffy::AlignItemsKeyword::Start);
        taffy::Style center =
            leaf(50.0f, 20.0f, taffy::AlignItemsKeyword::Center);
        taffy::Style plain;
        plain.display = taffy::Display::Block;
        plain.size = SizeDim::FromLengths(50.0f, 20.0f);
        TaffyNodeId kids[3] = {tree.NewLeaf(start), tree.NewLeaf(center),
                               tree.NewLeaf(plain)};
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.textAlign = taffy::TextAlign::LegacyRight;
        root.size = SizeDim::FromLengths(200.0f, 100.0f);
        TaffyNodeId node = tree.NewWithChildren(root, kids, 3);
        tree.ComputeLayout(node, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(kids[0]).location.x, 0.0f);
        utassertnear(tree.GetLayout(kids[1]).location.x, 75.0f);
        utassertnear(tree.GetLayout(kids[2]).location.x, 150.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style plain;
        plain.display = taffy::Display::Block;
        plain.size = SizeDim::FromLengths(50.0f, 20.0f);
        taffy::Style center =
            leaf(50.0f, 20.0f, taffy::AlignItemsKeyword::Center);
        TaffyNodeId kids[3] = {tree.NewLeaf(plain), tree.NewLeaf(center),
                               tree.NewLeaf(plain)};
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.justifyItems = taffy::AlignItems{taffy::AlignItemsKeyword::End};
        root.size = SizeDim::FromLengths(200.0f, 100.0f);
        TaffyNodeId node = tree.NewWithChildren(root, kids, 3);
        tree.ComputeLayout(node, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(kids[0]).location.x, 150.0f);
        utassertnear(tree.GetLayout(kids[1]).location.x, 75.0f);
        utassertnear(tree.GetLayout(kids[2]).location.x, 150.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style centered =
            leaf(50.0f, 20.0f, taffy::AlignItemsKeyword::Center);
        centered.margin.left = LengthPercentageAuto::Length(10.0f);
        centered.margin.right = LengthPercentageAuto::Length(30.0f);
        taffy::Style ended = leaf(50.0f, 20.0f, taffy::AlignItemsKeyword::End);
        ended.margin.right = LengthPercentageAuto::Length(10.0f);
        taffy::Style started =
            leaf(50.0f, 20.0f, taffy::AlignItemsKeyword::Start);
        started.margin.left = LengthPercentageAuto::Length(10.0f);
        TaffyNodeId kids[3] = {tree.NewLeaf(centered), tree.NewLeaf(ended),
                               tree.NewLeaf(started)};
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.size = SizeDim::FromLengths(200.0f, 100.0f);
        TaffyNodeId node = tree.NewWithChildren(root, kids, 3);
        tree.ComputeLayout(node, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(kids[0]).location.x, 65.0f);
        utassertnear(tree.GetLayout(kids[1]).location.x, 140.0f);
        utassertnear(tree.GetLayout(kids[2]).location.x, 10.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style inner;
        inner.display = taffy::Display::Block;
        inner.size = SizeDim::FromLengths(60.0f, 20.0f);
        TaffyNodeId innerCenter = tree.NewLeaf(inner);
        taffy::Style fitCenter;
        fitCenter.display = taffy::Display::Block;
        fitCenter.size.height = taffy::Dimension::Length(20.0f);
        fitCenter.justifySelf = taffy::OptAlignSelf(
            taffy::AlignItems{taffy::AlignItemsKeyword::Center});
        TaffyNodeId center = tree.NewWithChildren(fitCenter, &innerCenter, 1);
        taffy::Style innerEnd;
        innerEnd.display = taffy::Display::Block;
        innerEnd.size = SizeDim::FromLengths(60.0f, 10.0f);
        TaffyNodeId innerEndNode = tree.NewLeaf(innerEnd);
        taffy::Style fitEnd;
        fitEnd.display = taffy::Display::Block;
        fitEnd.size.height = taffy::Dimension::Length(20.0f);
        fitEnd.padding.left = taffy::LengthPercentage::Length(5.0f);
        fitEnd.padding.right = taffy::LengthPercentage::Length(5.0f);
        fitEnd.justifySelf = taffy::OptAlignSelf(
            taffy::AlignItems{taffy::AlignItemsKeyword::End});
        TaffyNodeId end = tree.NewWithChildren(fitEnd, &innerEndNode, 1);
        taffy::Style stretched;
        stretched.display = taffy::Display::Block;
        stretched.size.height = taffy::Dimension::Length(20.0f);
        stretched.justifySelf = taffy::OptAlignSelf(
            taffy::AlignItems{taffy::AlignItemsKeyword::Stretch});
        TaffyNodeId stretchNode = tree.NewLeaf(stretched);
        TaffyNodeId kids[3] = {center, end, stretchNode};
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.size = SizeDim::FromLengths(200.0f, 100.0f);
        TaffyNodeId node = tree.NewWithChildren(root, kids, 3);
        tree.ComputeLayout(node, SizeAvail::MaxContent());
        Layout centerBox = tree.GetLayout(center);
        utassertnear(centerBox.location.x, 70.0f);
        utassertnear(centerBox.size.w, 60.0f);
        Layout endBox = tree.GetLayout(end);
        utassertnear(endBox.location.x, 130.0f);
        utassertnear(endBox.size.w, 70.0f);
        Layout stretchBox = tree.GetLayout(stretchNode);
        utassertnear(stretchBox.location.x, 0.0f);
        utassertnear(stretchBox.size.w, 200.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style replaced;
        replaced.display = taffy::Display::Block;
        replaced.itemIsReplaced = true;
        replaced.size.height = taffy::Dimension::Length(10.0f);
        replaced.justifySelf = taffy::OptAlignSelf(
            taffy::AlignItems{taffy::AlignItemsKeyword::Stretch});
        TaffyNodeId stretchNode = tree.NewLeaf(replaced);
        replaced.justifySelf = taffy::OptAlignSelf(
            taffy::AlignItems{taffy::AlignItemsKeyword::Start});
        TaffyNodeId startNode = tree.NewLeaf(replaced);
        TaffyNodeId kids[2] = {stretchNode, startNode};
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.size = SizeDim::FromLengths(200.0f, 40.0f);
        TaffyNodeId node = tree.NewWithChildren(root, kids, 2);
        tree.ComputeLayout(node, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(stretchNode).size.w, 200.0f);
        utassertnear(tree.GetLayout(startNode).size.w, 0.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style floated;
        floated.display = taffy::Display::Block;
        floated.floatMode = taffy::Float::Left;
        floated.size = SizeDim::FromLengths(40.0f, 40.0f);
        TaffyNodeId floatNode = tree.NewLeaf(floated);
        taffy::Style beside;
        beside.display = taffy::Display::FlowRoot;
        beside.size = SizeDim::FromLengths(40.0f, 20.0f);
        beside.justifySelf = taffy::OptAlignSelf(
            taffy::AlignItems{taffy::AlignItemsKeyword::End});
        TaffyNodeId besideNode = tree.NewLeaf(beside);
        TaffyNodeId kids[2] = {floatNode, besideNode};
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.size.width = taffy::Dimension::Length(200.0f);
        TaffyNodeId node = tree.NewWithChildren(root, kids, 2);
        tree.ComputeLayout(node, SizeAvail::MaxContent());
        // The leftover slot is 40..200. End places the 40px box at 160.
        utassertnear(tree.GetLayout(besideNode).location.x, 160.0f);
        utassertnear(tree.GetLayout(besideNode).location.y, 0.0f);
        tree.Free();
    }
}

// block_percentage_height_with_min_height_parent and the relative
// percentage-inset fixtures. Min-height is not a percentage basis, and
// a relative percentage inset resolves against the container's height.
static void TestBlockPercentageBasis() {
    TestSuite("taffy block percentage basis");

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style grand;
        grand.display = taffy::Display::Block;
        grand.size = SizeDim::FromLengths(20.0f, 20.0f);
        TaffyNodeId g = tree.NewLeaf(grand);
        taffy::Style mid;
        mid.display = taffy::Display::Block;
        mid.size.height = taffy::Dimension::Percent(0.5f);
        TaffyNodeId m = tree.NewWithChildren(mid, &g, 1);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.size.width = taffy::Dimension::Length(100.0f);
        rootStyle.minSize.height = taffy::Dimension::Length(80.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &m, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(root).size.h, 80.0f);
        utassertnear(tree.GetLayout(m).size.h, 20.0f);
        utassertnear(tree.GetLayout(g).size.h, 20.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style childStyle;
        childStyle.display = taffy::Display::Block;
        childStyle.position = taffy::Position::Relative;
        childStyle.size = SizeDim::FromLengths(20.0f, 20.0f);
        childStyle.inset.top = LengthPercentageAuto::Percent(0.5f);
        TaffyNodeId child = tree.NewLeaf(childStyle);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.size.width = taffy::Dimension::Length(100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(root).size.h, 20.0f);
        utassertnear(tree.GetLayout(child).location.y, 0.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style childStyle;
        childStyle.display = taffy::Display::Block;
        childStyle.position = taffy::Position::Relative;
        childStyle.size = SizeDim::FromLengths(20.0f, 20.0f);
        childStyle.inset.top = LengthPercentageAuto::Percent(0.5f);
        childStyle.inset.left = LengthPercentageAuto::Percent(0.1f);
        TaffyNodeId child = tree.NewLeaf(childStyle);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.size = SizeDim::FromLengths(100.0f, 200.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(child).location.x, 10.0f);
        utassertnear(tree.GetLayout(child).location.y, 100.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style grand;
        grand.display = taffy::Display::Block;
        grand.size = SizeDim::FromLengths(50.0f, 50.0f);
        TaffyNodeId g = tree.NewLeaf(grand);
        taffy::Style pct;
        pct.display = taffy::Display::Block;
        pct.size.height = taffy::Dimension::Percent(0.5f);
        TaffyNodeId p = tree.NewWithChildren(pct, &g, 1);
        taffy::Style itemStyle;
        itemStyle.display = taffy::Display::Block;
        itemStyle.size.width = taffy::Dimension::Length(50.0f);
        itemStyle.minSize.height = taffy::Dimension::Length(0.0f);
        TaffyNodeId item = tree.NewWithChildren(itemStyle, &p, 1);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Flex;
        rootStyle.size.width = taffy::Dimension::Length(100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &item, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(root).size.h, 50.0f);
        utassertnear(tree.GetLayout(item).size.h, 50.0f);
        utassertnear(tree.GetLayout(p).size.h, 25.0f);
        utassertnear(tree.GetLayout(g).size.h, 50.0f);
        tree.Free();
    }
}

// A stretched flex item's cross size is definite, so a percentage height
// resolves. An unstretched item's cross height stays indefinite.
static void TestFlexCrossDefinitePercentage() {
    TestSuite("taffy flex cross size definiteness");

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style pct;
        pct.display = taffy::Display::Block;
        pct.size.height = taffy::Dimension::Percent(0.5f);
        TaffyNodeId p = tree.NewLeaf(pct);
        taffy::Style itemStyle;
        itemStyle.display = taffy::Display::Block;
        itemStyle.size.width = taffy::Dimension::Length(50.0f);
        TaffyNodeId item = tree.NewWithChildren(itemStyle, &p, 1);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Flex;
        rootStyle.size = SizeDim::FromLengths(100.0f, 100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &item, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(item).size.h, 100.0f);
        utassertnear(tree.GetLayout(p).size.h, 50.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style grand;
        grand.display = taffy::Display::Block;
        grand.size = SizeDim::FromLengths(20.0f, 20.0f);
        TaffyNodeId g = tree.NewLeaf(grand);
        taffy::Style pct;
        pct.display = taffy::Display::Block;
        pct.size.height = taffy::Dimension::Percent(0.5f);
        TaffyNodeId p = tree.NewWithChildren(pct, &g, 1);
        taffy::Style itemStyle;
        itemStyle.display = taffy::Display::Block;
        itemStyle.size.width = taffy::Dimension::Length(50.0f);
        TaffyNodeId item = tree.NewWithChildren(itemStyle, &p, 1);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Flex;
        rootStyle.alignItems = AlignItems{AlignItemsKeyword::FlexStart};
        rootStyle.size = SizeDim::FromLengths(100.0f, 100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &item, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(item).size.h, 20.0f);
        utassertnear(tree.GetLayout(p).size.h, 20.0f);
        utassertnear(tree.GetLayout(g).size.h, 20.0f);
        tree.Free();
    }
}

// ─── src/compute/grid/explicit_grid.rs ───────────────────────────────────
// flex_container_baseline_column_reverse. The container baseline comes
// from the visually topmost item, which is last in column-reverse order.
static void TestFlexReverseBaseline() {
    TestSuite("taffy flex column-reverse baseline");
    TaffyTree tree;
    tree.Init();
    taffy::Style tall;
    tall.size = SizeDim::FromLengths(50.0f, 100.0f);
    TaffyNodeId tallNode = tree.NewLeaf(tall);

    taffy::Style top;
    top.size = SizeDim::FromLengths(60.0f, 20.0f);
    TaffyNodeId topNode = tree.NewLeaf(top);
    taffy::Style below;
    below.size = SizeDim::FromLengths(60.0f, 30.0f);
    TaffyNodeId belowNode = tree.NewLeaf(below);
    TaffyNodeId colKids[] = {topNode, belowNode};
    taffy::Style col;
    col.flexDirection = FlexDirection::ColumnReverse;
    col.size = SizeDim::FromLengths(60.0f, 90.0f);
    TaffyNodeId colNode = tree.NewWithChildren(col, colKids, 2);

    taffy::Style rootStyle;
    rootStyle.alignItems = AlignItems{AlignItemsKeyword::Baseline};
    rootStyle.size = SizeDim::FromLengths(200.0f, 200.0f);
    TaffyNodeId kids[] = {tallNode, colNode};
    TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
    tree.ComputeLayout(root, SizeAvail::MaxContent());
    utassertnear(tree.GetLayout(tallNode).location.y, 0.0f);
    utassertnear(tree.GetLayout(colNode).location.x, 50.0f);
    utassertnear(tree.GetLayout(colNode).location.y, 30.0f);
    utassertnear(tree.GetLayout(topNode).location.y, 70.0f);
    utassertnear(tree.GetLayout(belowNode).location.y, 40.0f);
    tree.Free();
}

//
// The grid internals are private; compute.h exposes the seams taffy's own
// unit tests reach into. See the note there.

// The arena the grid track lists in these tests are built from. A `Style`
// holds arena-backed slices rather than owning `Vec`s (see src/taffy/style.h),
// so the test owns the arena instead.
static Arena* gGridArena = nullptr;

// Rust's `repeat(count, tracks)`.
static GridTemplateComponent GridRepeat(RepetitionCount count,
                                        const TrackSizingFunction* fns, int n) {
    GridTemplateRepetition r;
    r.count = count;
    r.tracks = SliceDup(gGridArena, fns, n);
    return GridTemplateComponent::Repeat(r);
}

static Slice<GridTemplateComponent> GridTemplateOf(
    const GridTemplateComponent* comps, int n) {
    return SliceDup(gGridArena, comps, n);
}

static void TestGridRepetitionLineNames() {
    TestSuite("taffy::grid named lines");

    // repeat() with no line-name sets still occupies its tracks, so a
    // name on the line after the repetition resolves past those tracks.
    TaffyTree tree;
    tree.Init();
    taffy::Style childStyle;
    childStyle.size = SizeDim::FromLengths(5.0f, 5.0f);
    childStyle.gridColumn.start = GridPlacement::FromNamedLine(StrL("c"), 1);
    childStyle.gridColumn.end = GridPlacement::Auto();
    TaffyNodeId child = tree.NewLeaf(childStyle);

    TrackSizingFunction ten = TrackSizingFunction::Length(10.0f);
    GridTemplateComponent cols[2];
    cols[0] = GridRepeat(RepetitionCount::Exactly(2), &ten, 1);
    cols[1] = GridTemplateComponent::Single(ten);

    Str cName = StrL("c");
    LineNameSet names[3];
    names[2].names = SliceDup(gGridArena, &cName, 1);

    taffy::Style rootStyle;
    rootStyle.display = taffy::Display::Grid;
    rootStyle.size = SizeDim::FromLengths(100.0f, 100.0f);
    rootStyle.gridTemplateColumns = GridTemplateOf(cols, 2);
    rootStyle.gridTemplateColumnNames = SliceDup(gGridArena, names, 3);
    TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
    tree.ComputeLayout(root, SizeAvail::Definite({100.0f, 100.0f}));

    utassertnear(tree.GetLayout(child).location.x, 30.0f);
    tree.Free();
}

// One child on a 4×10px column grid. `x / 10` is the column start line minus
// one when every track, including implicit ones, is 10px.
static Layout PlaceAreaEdgeChild(LineNameSet names[5], bool withArea,
                                 LinePlacement column, LinePlacement row) {
    TaffyTree tree;
    tree.Init();
    taffy::Style child;
    child.gridColumn = column;
    child.gridRow = row;
    TaffyNodeId node = tree.NewLeaf(child);

    TrackSizingFunction ten = TrackSizingFunction::Length(10.0f);
    GridTemplateComponent cols[4];
    for (int i = 0; i < 4; i++) {
        cols[i] = GridTemplateComponent::Single(ten);
    }
    GridTemplateComponent rowTrack = GridTemplateComponent::Single(ten);
    taffy::Style root;
    root.display = taffy::Display::Grid;
    root.gridTemplateColumns = GridTemplateOf(cols, 4);
    root.gridTemplateRows = GridTemplateOf(&rowTrack, 1);
    root.gridTemplateColumnNames = SliceDup(gGridArena, names, 5);
    root.gridAutoColumns = SliceDup(gGridArena, &ten, 1);
    root.gridAutoRows = SliceDup(gGridArena, &ten, 1);
    if (withArea) {
        GridTemplateArea area;
        area.name = StrL("area");
        area.rowStart = 1;
        area.rowEnd = 2;
        area.columnStart = 2;
        area.columnEnd = 4;
        GridTemplateAreas areas;
        areas.areas = SliceDup(gGridArena, &area, 1);
        areas.rowCount = 1;
        areas.columnCount = 4;
        root.gridTemplateAreas = areas;
    }
    TaffyNodeId rootNode = tree.NewWithChildren(root, &node, 1);
    tree.ComputeLayout(rootNode, SizeAvail::MaxContent());
    Layout layout = tree.GetLayout(node);
    tree.Free();
    return layout;
}

static LineNameSet NameSet(const char* a) {
    LineNameSet set;
    Str name = Str(a);
    set.names = SliceDup(gGridArena, &name, 1);
    return set;
}

static GridPlacement GNamed(const char* name, int16_t index) {
    return GridPlacement::FromNamedLine(Str(name), index);
}
static GridPlacement GNamedSpan(const char* name, uint16_t span) {
    return GridPlacement::FromNamedSpan(Str(name), span);
}

static void TestGridAreaEdges() {
    TestSuite("taffy grid area edges");
    LineNameSet none[5];
    Layout box;

    box = PlaceAreaEdgeChild(none, true, {GNamed("area", 0), GNamed("area", 0)},
                             {GNamed("area", 0), GNamed("area", 0)});
    AssertBox(box, 10.0f, 0.0f, 20.0f, 10.0f);

    LineNameSet implicit[5];
    implicit[1] = NameSet("foo-start");
    implicit[2] = NameSet("foo-start");
    implicit[3] = NameSet("foo-end");
    implicit[4] = NameSet("foo-end");
    box = PlaceAreaEdgeChild(implicit, true,
                             {GNamed("foo", 0), GNamed("foo", 0)}, {});
    AssertBox(box, 10.0f, 0.0f, 20.0f, 10.0f);

    box = PlaceAreaEdgeChild(
        none, true, {GNamed("area-start", 0), GridPlacement::Auto()}, {});
    utassertnear(box.location.x, 10.0f);
    box = PlaceAreaEdgeChild(
        none, true, {GNamed("area-start", 2), GridPlacement::Auto()}, {});
    utassertnear(box.location.x, 50.0f);
    box = PlaceAreaEdgeChild(
        none, true, {GNamed("area-end", 1), GridPlacement::Auto()}, {});
    utassertnear(box.location.x, 30.0f);
    box = PlaceAreaEdgeChild(
        none, true, {GNamed("area-end", -1), GridPlacement::Auto()}, {});
    utassertnear(box.location.x, 30.0f);

    LineNameSet merged[5];
    merged[0] = NameSet("area-start");
    merged[2] = NameSet("area-start");
    merged[3] = NameSet("area-end");
    merged[4] = NameSet("area-end");
    float startX[5] = {0.0f, 10.0f, 20.0f, 20.0f, 50.0f};
    int16_t startIdx[5] = {1, 2, 3, -1, 4};
    for (int i = 0; i < 5; i++) {
        box = PlaceAreaEdgeChild(
            merged, true,
            {GNamed("area-start", startIdx[i]), GridPlacement::Auto()}, {});
        utassertnear(box.location.x, startX[i]);
    }
    box = PlaceAreaEdgeChild(
        merged, true, {GNamed("area-end", 1), GridPlacement::Auto()}, {});
    utassertnear(box.location.x, 30.0f);
    box = PlaceAreaEdgeChild(
        merged, true, {GNamed("area-end", 2), GridPlacement::Auto()}, {});
    utassertnear(box.location.x, 40.0f);
    box = PlaceAreaEdgeChild(
        merged, true, {GNamed("area-end", 3), GridPlacement::Auto()}, {});
    utassertnear(box.location.x, 50.0f);
    box = PlaceAreaEdgeChild(merged, true,
                             {GNamed("area", 0), GNamed("area", 0)}, {});
    AssertBox(box, 0.0f, 0.0f, 30.0f, 10.0f);
    box = PlaceAreaEdgeChild(
        merged, true,
        {GridPlacement::FromLineIndex(1), GNamedSpan("area-start", 2)}, {});
    AssertBox(box, 0.0f, 0.0f, 20.0f, 10.0f);

    box = PlaceAreaEdgeChild(none, true,
                             {GNamed("area", 1), GridPlacement::Auto()}, {});
    utassertnear(box.location.x, 50.0f);
    box = PlaceAreaEdgeChild(none, true,
                             {GNamed("area", 2), GridPlacement::Auto()}, {});
    utassertnear(box.location.x, 60.0f);
    box = PlaceAreaEdgeChild(
        none, true, {GridPlacement::FromLineIndex(1), GNamedSpan("area", 0)},
        {});
    AssertBox(box, 0.0f, 0.0f, 50.0f, 10.0f);

    LineNameSet later[5];
    later[2] = NameSet("area-start");
    later[4] = NameSet("area-end");
    box = PlaceAreaEdgeChild(later, true,
                             {GNamed("area", 0), GNamed("area", 0)}, {});
    AssertBox(box, 10.0f, 0.0f, 20.0f, 10.0f);
    box = PlaceAreaEdgeChild(
        later, true, {GNamed("area-start", 1), GridPlacement::Auto()}, {});
    utassertnear(box.location.x, 10.0f);
    box = PlaceAreaEdgeChild(
        later, true, {GNamed("area-start", 2), GridPlacement::Auto()}, {});
    utassertnear(box.location.x, 20.0f);
    box = PlaceAreaEdgeChild(
        later, true, {GNamed("area-end", 2), GridPlacement::Auto()}, {});
    utassertnear(box.location.x, 40.0f);

    LineNameSet same[5];
    same[1] = NameSet("area-start");
    same[3] = NameSet("area-end");
    box = PlaceAreaEdgeChild(
        same, true, {GNamed("area-start", 2), GridPlacement::Auto()}, {});
    utassertnear(box.location.x, 50.0f);
    box = PlaceAreaEdgeChild(
        same, true, {GNamed("area-end", 2), GridPlacement::Auto()}, {});
    utassertnear(box.location.x, 50.0f);
}

// A non-finite flex factor or container size used to keep the fr-size and
// space-distribution loops from ever deciding they were done.
static void TestGridNonFiniteStylesTerminate() {
    TestSuite("taffy::grid non-finite styles");

    float factors[3] = {NAN, INFINITY, -INFINITY};
    for (int i = 0; i < 3; i++) {
        TaffyTree tree;
        tree.Init();
        taffy::Style childStyle;
        childStyle.size = SizeDim::FromLengths(10.0f, 10.0f);
        TaffyNodeId child = tree.NewLeaf(childStyle);

        GridTemplateComponent col =
            GridTemplateComponent::Single(TrackSizingFunction::Fr(factors[i]));
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Grid;
        rootStyle.gridTemplateColumns = GridTemplateOf(&col, 1);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
        tree.ComputeLayout(root, SizeAvail::Definite({100.0f, 100.0f}));
        // The item's height is a length, so stretch does not replace it.
        utassertnear(tree.GetLayout(child).size.h, 10.0f);
        tree.Free();
    }

    float widths[2] = {NAN, INFINITY};
    for (int i = 0; i < 2; i++) {
        TaffyTree tree;
        tree.Init();
        TaffyNodeId child = tree.NewLeaf(taffy::Style{});
        TrackSizingFunction fr = TrackSizingFunction::Fr(1.0f);
        GridTemplateComponent rep =
            GridRepeat(RepetitionCount::Exactly(2), &fr, 1);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Grid;
        rootStyle.size = SizeDim::FromLengths(widths[i], 100.0f);
        rootStyle.gridTemplateColumns = GridTemplateOf(&rep, 1);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
        tree.ComputeLayout(root, SizeAvail::Definite({100.0f, 100.0f}));
        utassertnear(tree.GetLayout(root).size.h, 100.0f);
        tree.Free();
    }
}

// A zero span is normalized to 1. Paired with a named span that names
// nothing, that used to estimate a zero-track grid while auto-placement
// searched for a span of 1, and the search never finished.
static void TestGridZeroSpanTerminates() {
    TestSuite("taffy::grid zero span");

    Direction directions[2] = {Direction::Ltr, Direction::Rtl};
    for (int i = 0; i < 2; i++) {
        TaffyTree tree;
        tree.Init();
        taffy::Style childStyle;
        childStyle.gridColumn
            .start = GridPlacement::FromNamedSpan(StrL("does-not-exist"), 1);
        childStyle.gridColumn.end = GridPlacement::FromSpan(0);
        TaffyNodeId child = tree.NewLeaf(childStyle);

        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Grid;
        rootStyle.direction = directions[i];
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
        tree.ComputeLayout(root, SizeAvail::Definite({100.0f, 100.0f}));
        utassert(tree.GetLayout(root).size.w >= 0.0f);
        utassert(tree.GetLayout(root).size.h >= 0.0f);
        tree.Free();
    }
}

// minmax(auto, 0px) still takes the item's intrinsic size. A fixed max
// does not let the track-sizing pass skip an intrinsic min.
static void TestGridMinmaxAutoFixedZero() {
    TestSuite("taffy::grid minmax auto fixed");

    Direction directions[2] = {Direction::Ltr, Direction::Rtl};
    float xs[2] = {0.0f, 40.0f};
    for (int i = 0; i < 2; i++) {
        TaffyTree tree;
        tree.Init();
        taffy::Style wide;
        wide.boxSizing = BoxSizing::ContentBox;
        wide.direction = directions[i];
        wide.size.width = Dimension::Length(60.0f);
        TaffyNodeId a = tree.NewLeaf(wide);
        taffy::Style empty;
        empty.boxSizing = BoxSizing::ContentBox;
        empty.direction = directions[i];
        TaffyNodeId b = tree.NewLeaf(empty);

        TrackSizingFunction col;
        col.min = MinTrackSizingFunction::Auto();
        col.max = MaxTrackSizingFunction::Length(0.0f);
        TrackSizingFunction rowPx = TrackSizingFunction::Length(10.0f);
        GridTemplateComponent colC = GridTemplateComponent::Single(col);
        GridTemplateComponent rowC[2] = {GridTemplateComponent::Single(rowPx),
                                         GridTemplateComponent::Single(rowPx)};

        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Grid;
        rootStyle.boxSizing = BoxSizing::ContentBox;
        rootStyle.direction = directions[i];
        rootStyle.size.width = Dimension::Length(100.0f);
        rootStyle.gridTemplateColumns = GridTemplateOf(&colC, 1);
        rootStyle.gridTemplateRows = GridTemplateOf(rowC, 2);
        TaffyNodeId kids[2] = {a, b};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        utassertnear(tree.GetLayout(root).size.w, 100.0f);
        utassertnear(tree.GetLayout(root).size.h, 20.0f);
        utassertnear(tree.GetLayout(a).location.x, xs[i]);
        utassertnear(tree.GetLayout(a).location.y, 0.0f);
        utassertnear(tree.GetLayout(a).size.w, 60.0f);
        utassertnear(tree.GetLayout(a).size.h, 10.0f);
        utassertnear(tree.GetLayout(b).location.x, xs[i]);
        utassertnear(tree.GetLayout(b).location.y, 10.0f);
        utassertnear(tree.GetLayout(b).size.w, 60.0f);
        utassertnear(tree.GetLayout(b).size.h, 10.0f);
        tree.Free();
    }
}

// Width and height sizing keywords on a grid item. The fixed tracks make the
// area definite, so stretch is the area minus margins and the intrinsic
// keywords measure the fixture text (min 20x20, max 40x10).
static void TestGridSizingKeywords() {
    TestSuite("taffy::grid sizing keywords");

    {
        // grid_item_width_stretch_margin, content-box. Height is auto, so
        // the row's stretch alignment subtracts the vertical margins.
        TaffyTree tree;
        tree.Init();
        taffy::Style childStyle;
        childStyle.boxSizing = BoxSizing::ContentBox;
        childStyle.size.width = Dimension::Stretch();
        childStyle.margin.top = LengthPercentageAuto::Length(5.0f);
        childStyle.margin.right = LengthPercentageAuto::Length(10.0f);
        childStyle.margin.bottom = LengthPercentageAuto::Length(5.0f);
        childStyle.margin.left = LengthPercentageAuto::Length(15.0f);
        TaffyNodeId child = tree.NewLeaf(childStyle);

        TrackSizingFunction col = TrackSizingFunction::Length(120.0f);
        TrackSizingFunction row = TrackSizingFunction::Length(40.0f);
        GridTemplateComponent colC = GridTemplateComponent::Single(col);
        GridTemplateComponent rowC = GridTemplateComponent::Single(row);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Grid;
        rootStyle.boxSizing = BoxSizing::ContentBox;
        rootStyle.gridTemplateColumns = GridTemplateOf(&colC, 1);
        rootStyle.gridTemplateRows = GridTemplateOf(&rowC, 1);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 120.0f, 40.0f);
        AssertBox(tree.GetLayout(child), 15.0f, 5.0f, 95.0f, 30.0f);
        tree.Free();
    }

    {
        // grid_item_width_keywords, content-box.
        Direction directions[2] = {Direction::Ltr, Direction::Rtl};
        float fitX[2] = {0.0f, 80.0f};
        float minX[2] = {0.0f, 100.0f};
        float maxX[2] = {0.0f, 80.0f};
        for (int i = 0; i < 2; i++) {
            TaffyTree tree;
            tree.Init();
            taffy::Style stretchStyle;
            stretchStyle.boxSizing = BoxSizing::ContentBox;
            stretchStyle.direction = directions[i];
            stretchStyle.size.width = Dimension::Stretch();
            taffy::Style fitStyle = stretchStyle;
            fitStyle.size.width = Dimension::FitContent();
            taffy::Style minStyle = stretchStyle;
            minStyle.size.width = Dimension::MinContent();
            taffy::Style maxStyle = stretchStyle;
            maxStyle.size.width = Dimension::MaxContent();
            TaffyNodeId a = tree.NewLeaf(stretchStyle);
            TaffyNodeId b = tree.NewLeaf(fitStyle);
            TaffyNodeId c = tree.NewLeaf(minStyle);
            TaffyNodeId d = tree.NewLeaf(maxStyle);

            TrackSizingFunction col = TrackSizingFunction::Length(120.0f);
            TrackSizingFunction row = TrackSizingFunction::Length(30.0f);
            GridTemplateComponent colC = GridTemplateComponent::Single(col);
            taffy::Style rootStyle;
            rootStyle.display = taffy::Display::Grid;
            rootStyle.boxSizing = BoxSizing::ContentBox;
            rootStyle.direction = directions[i];
            rootStyle.gridTemplateColumns = GridTemplateOf(&colC, 1);
            rootStyle.gridAutoRows = SliceDup(gGridArena, &row, 1);
            TaffyNodeId kids[4] = {a, b, c, d};
            TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 4);
            tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                          KeywordTextMeasure, nullptr);
            AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 120.0f, 120.0f);
            AssertBox(tree.GetLayout(a), 0.0f, 0.0f, 120.0f, 30.0f);
            AssertBox(tree.GetLayout(b), fitX[i], 30.0f, 40.0f, 30.0f);
            AssertBox(tree.GetLayout(c), minX[i], 60.0f, 20.0f, 30.0f);
            AssertBox(tree.GetLayout(d), maxX[i], 90.0f, 40.0f, 30.0f);
            tree.Free();
        }
    }

    {
        // grid_item_height_keywords, content-box. The 40px column fits the
        // text on one line, so every intrinsic height is 10.
        TaffyTree tree;
        tree.Init();
        taffy::Style stretchStyle;
        stretchStyle.boxSizing = BoxSizing::ContentBox;
        stretchStyle.size.height = Dimension::Stretch();
        taffy::Style fitStyle = stretchStyle;
        fitStyle.size.height = Dimension::FitContent();
        taffy::Style minStyle = stretchStyle;
        minStyle.size.height = Dimension::MinContent();
        taffy::Style maxStyle = stretchStyle;
        maxStyle.size.height = Dimension::MaxContent();
        TaffyNodeId a = tree.NewLeaf(stretchStyle);
        TaffyNodeId b = tree.NewLeaf(fitStyle);
        TaffyNodeId c = tree.NewLeaf(minStyle);
        TaffyNodeId d = tree.NewLeaf(maxStyle);

        TrackSizingFunction col = TrackSizingFunction::Length(40.0f);
        TrackSizingFunction row = TrackSizingFunction::Length(60.0f);
        GridTemplateComponent cols[4] = {GridTemplateComponent::Single(col),
                                         GridTemplateComponent::Single(col),
                                         GridTemplateComponent::Single(col),
                                         GridTemplateComponent::Single(col)};
        GridTemplateComponent rowC = GridTemplateComponent::Single(row);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Grid;
        rootStyle.boxSizing = BoxSizing::ContentBox;
        rootStyle.gridTemplateColumns = GridTemplateOf(cols, 4);
        rootStyle.gridTemplateRows = GridTemplateOf(&rowC, 1);
        TaffyNodeId kids[4] = {a, b, c, d};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 4);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      KeywordTextMeasure, nullptr);
        AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 160.0f, 60.0f);
        AssertBox(tree.GetLayout(a), 0.0f, 0.0f, 40.0f, 60.0f);
        AssertBox(tree.GetLayout(b), 40.0f, 0.0f, 40.0f, 10.0f);
        AssertBox(tree.GetLayout(c), 80.0f, 0.0f, 40.0f, 10.0f);
        AssertBox(tree.GetLayout(d), 120.0f, 0.0f, 40.0f, 10.0f);
        tree.Free();
    }
}

// Rust's `(width, height, cols, rows).into_grid()`: a grid of `1fr` tracks.
static taffy::Style GridParent(float w, float h, int cols, int rows) {
    taffy::Style s;
    s.display = taffy::Display::Grid;
    s.size = SizeDim::FromLengths(w, h);
    Slice<GridTemplateComponent> c =
        SliceNew<GridTemplateComponent>(gGridArena, cols);
    for (int i = 0; i < cols; i++) {
        c[i] = GridTemplateComponent::Single(TrackSizingFunction::Fr(1.0f));
    }
    Slice<GridTemplateComponent> r =
        SliceNew<GridTemplateComponent>(gGridArena, rows);
    for (int i = 0; i < rows; i++) {
        r[i] = GridTemplateComponent::Single(TrackSizingFunction::Fr(1.0f));
    }
    s.gridTemplateColumns = c;
    s.gridTemplateRows = r;
    return s;
}

// Rust's `(col_start, col_end, row_start, row_end).into_grid_child()`.
static taffy::Style GridChild(GridPlacement cs, GridPlacement ce,
                              GridPlacement rs, GridPlacement re) {
    taffy::Style s;
    s.display = taffy::Display::Grid;
    s.gridColumn = {cs, ce};
    s.gridRow = {rs, re};
    return s;
}

static GridPlacement GLine(int16_t i) {
    return GridPlacement::FromLineIndex(i);
}
static GridPlacement GSpan(uint16_t n) {
    return GridPlacement::FromSpan(n);
}
static GridPlacement GAuto() {
    return GridPlacement::Auto();
}

struct ExplicitSizeResult {
    uint16_t colReps = 0;
    uint16_t colCount = 0;
    uint16_t rowReps = 0;
    uint16_t rowCount = 0;
};

static ExplicitSizeResult RunExplicitSize(const taffy::Style& style,
                                          SizeFOpt containerSize,
                                          bool maxRepetitions) {
    ExplicitSizeResult r;
    GridExplicitSizeForTest(style, containerSize.w, maxRepetitions,
                            AbsoluteAxis::Horizontal, NoCalc(), &r.colReps,
                            &r.colCount);
    GridExplicitSizeForTest(style, containerSize.h, maxRepetitions,
                            AbsoluteAxis::Vertical, NoCalc(), &r.rowReps,
                            &r.rowCount);
    return r;
}

// The preferred size as an optional, which is what Rust's tests pass in.
static SizeFOpt PreferredSize(const taffy::Style& s) {
    return {s.size.width.IntoOption(), s.size.height.IntoOption()};
}

static void TestExplicitGridSizing() {
    TestSuite("taffy::compute::grid::explicit_grid (sizing)");
    const bool kMax = true;
    const bool kMin = false;

    {
        // explicit_grid_sizing_no_repeats
        taffy::Style s = GridParent(600.0f, 600.0f, 2, 4);
        ExplicitSizeResult r = RunExplicitSize(s, PreferredSize(s), kMax);
        utassert(r.colCount == 2);
        utassert(r.rowCount == 4);
        utassert(r.colReps == 0);
        utassert(r.rowReps == 0);
    }

    {
        // explicit_grid_sizing_auto_fill_exact_fit
        taffy::Style s;
        s.display = taffy::Display::Grid;
        s.size = SizeDim::FromLengths(120.0f, 80.0f);
        TrackSizingFunction col = TrackSizingFunction::Length(40.0f);
        TrackSizingFunction row = TrackSizingFunction::Length(20.0f);
        GridTemplateComponent c =
            GridRepeat(RepetitionCount::AutoFill(), &col, 1);
        GridTemplateComponent rr =
            GridRepeat(RepetitionCount::AutoFill(), &row, 1);
        s.gridTemplateColumns = GridTemplateOf(&c, 1);
        s.gridTemplateRows = GridTemplateOf(&rr, 1);
        ExplicitSizeResult r = RunExplicitSize(s, PreferredSize(s), kMax);
        utassert(r.colCount == 3);
        utassert(r.rowCount == 4);
        utassert(r.colReps == 3);
        utassert(r.rowReps == 4);
    }

    {
        // explicit_grid_sizing_auto_fill_non_exact_fit
        taffy::Style s;
        s.display = taffy::Display::Grid;
        s.size = SizeDim::FromLengths(140.0f, 90.0f);
        TrackSizingFunction col = TrackSizingFunction::Length(40.0f);
        TrackSizingFunction row = TrackSizingFunction::Length(20.0f);
        GridTemplateComponent c =
            GridRepeat(RepetitionCount::AutoFill(), &col, 1);
        GridTemplateComponent rr =
            GridRepeat(RepetitionCount::AutoFill(), &row, 1);
        s.gridTemplateColumns = GridTemplateOf(&c, 1);
        s.gridTemplateRows = GridTemplateOf(&rr, 1);
        ExplicitSizeResult r = RunExplicitSize(s, PreferredSize(s), kMax);
        utassert(r.colCount == 3);
        utassert(r.rowCount == 4);
        utassert(r.colReps == 3);
        utassert(r.rowReps == 4);
    }

    {
        // explicit_grid_sizing_auto_fill_min_size_exact_fit
        taffy::Style s;
        s.display = taffy::Display::Grid;
        s.minSize = SizeDim::FromLengths(120.0f, 80.0f);
        TrackSizingFunction col = TrackSizingFunction::Length(40.0f);
        TrackSizingFunction row = TrackSizingFunction::Length(20.0f);
        GridTemplateComponent c =
            GridRepeat(RepetitionCount::AutoFill(), &col, 1);
        GridTemplateComponent rr =
            GridRepeat(RepetitionCount::AutoFill(), &row, 1);
        s.gridTemplateColumns = GridTemplateOf(&c, 1);
        s.gridTemplateRows = GridTemplateOf(&rr, 1);
        ExplicitSizeResult r =
            RunExplicitSize(s, SizeFOpt{120.0f, 80.0f}, kMin);
        utassert(r.colCount == 3);
        utassert(r.rowCount == 4);
        utassert(r.colReps == 3);
        utassert(r.rowReps == 4);
    }

    {
        // explicit_grid_sizing_auto_fill_min_size_non_exact_fit
        taffy::Style s;
        s.display = taffy::Display::Grid;
        s.minSize = SizeDim::FromLengths(140.0f, 90.0f);
        TrackSizingFunction col = TrackSizingFunction::Length(40.0f);
        TrackSizingFunction row = TrackSizingFunction::Length(20.0f);
        GridTemplateComponent c =
            GridRepeat(RepetitionCount::AutoFill(), &col, 1);
        GridTemplateComponent rr =
            GridRepeat(RepetitionCount::AutoFill(), &row, 1);
        s.gridTemplateColumns = GridTemplateOf(&c, 1);
        s.gridTemplateRows = GridTemplateOf(&rr, 1);
        ExplicitSizeResult r =
            RunExplicitSize(s, SizeFOpt{140.0f, 90.0f}, kMin);
        utassert(r.colCount == 4);
        utassert(r.rowCount == 5);
        utassert(r.colReps == 4);
        utassert(r.rowReps == 5);
    }

    {
        // explicit_grid_sizing_auto_fill_multiple_repeated_tracks
        taffy::Style s;
        s.display = taffy::Display::Grid;
        s.size = SizeDim::FromLengths(140.0f, 100.0f);
        TrackSizingFunction cols[2] = {TrackSizingFunction::Length(40.0f),
                                       TrackSizingFunction::Length(20.0f)};
        TrackSizingFunction rows[2] = {TrackSizingFunction::Length(20.0f),
                                       TrackSizingFunction::Length(10.0f)};
        GridTemplateComponent c =
            GridRepeat(RepetitionCount::AutoFill(), cols, 2);
        GridTemplateComponent rr =
            GridRepeat(RepetitionCount::AutoFill(), rows, 2);
        s.gridTemplateColumns = GridTemplateOf(&c, 1);
        s.gridTemplateRows = GridTemplateOf(&rr, 1);
        ExplicitSizeResult r = RunExplicitSize(s, PreferredSize(s), kMax);
        // 2 repetitions x 2 repeated tracks, and 3 x 2.
        utassert(r.colCount == 4);
        utassert(r.rowCount == 6);
        utassert(r.colReps == 2);
        utassert(r.rowReps == 3);
    }

    {
        // explicit_grid_sizing_auto_fill_gap
        taffy::Style s;
        s.display = taffy::Display::Grid;
        s.size = SizeDim::FromLengths(140.0f, 100.0f);
        s.gap = {LengthPercentage::Length(20.0f),
                 LengthPercentage::Length(20.0f)};
        TrackSizingFunction col = TrackSizingFunction::Length(40.0f);
        TrackSizingFunction row = TrackSizingFunction::Length(20.0f);
        GridTemplateComponent c =
            GridRepeat(RepetitionCount::AutoFill(), &col, 1);
        GridTemplateComponent rr =
            GridRepeat(RepetitionCount::AutoFill(), &row, 1);
        s.gridTemplateColumns = GridTemplateOf(&c, 1);
        s.gridTemplateRows = GridTemplateOf(&rr, 1);
        ExplicitSizeResult r = RunExplicitSize(s, PreferredSize(s), kMax);
        utassert(r.colCount == 2); // 2 tracks + 1 gap
        utassert(r.rowCount == 3); // 3 tracks + 2 gaps
        utassert(r.colReps == 2);
        utassert(r.rowReps == 3);
    }

    {
        // explicit_grid_sizing_no_defined_size
        taffy::Style s;
        s.display = taffy::Display::Grid;
        s.gap = {LengthPercentage::Length(20.0f),
                 LengthPercentage::Length(20.0f)};
        TrackSizingFunction cols[3] = {TrackSizingFunction::Length(40.0f),
                                       TrackSizingFunction::Percent(0.5f),
                                       TrackSizingFunction::Length(20.0f)};
        TrackSizingFunction row = TrackSizingFunction::Length(20.0f);
        GridTemplateComponent c =
            GridRepeat(RepetitionCount::AutoFill(), cols, 3);
        GridTemplateComponent rr =
            GridRepeat(RepetitionCount::AutoFill(), &row, 1);
        s.gridTemplateColumns = GridTemplateOf(&c, 1);
        s.gridTemplateRows = GridTemplateOf(&rr, 1);
        ExplicitSizeResult r = RunExplicitSize(s, PreferredSize(s), kMin);
        utassert(r.colCount == 3);
        utassert(r.rowCount == 1);
        utassert(r.colReps == 1);
        utassert(r.rowReps == 1);
    }

    {
        // explicit_grid_sizing_mix_repeated_and_non_repeated
        taffy::Style s;
        s.display = taffy::Display::Grid;
        s.size = SizeDim::FromLengths(140.0f, 100.0f);
        s.gap = {LengthPercentage::Length(20.0f),
                 LengthPercentage::Length(20.0f)};
        TrackSizingFunction col = TrackSizingFunction::Length(40.0f);
        TrackSizingFunction row = TrackSizingFunction::Length(20.0f);
        GridTemplateComponent cs[2] = {
            GridTemplateComponent::Single(TrackSizingFunction::Length(20.0f)),
            GridRepeat(RepetitionCount::AutoFill(), &col, 1)};
        GridTemplateComponent rs[2] = {
            GridTemplateComponent::Single(TrackSizingFunction::Length(40.0f)),
            GridRepeat(RepetitionCount::AutoFill(), &row, 1)};
        s.gridTemplateColumns = GridTemplateOf(cs, 2);
        s.gridTemplateRows = GridTemplateOf(rs, 2);
        ExplicitSizeResult r = RunExplicitSize(s, PreferredSize(s), kMax);
        utassert(r.colCount == 3); // 3 tracks + 2 gaps
        utassert(r.rowCount == 2); // 2 tracks + 1 gap
        utassert(r.colReps == 2);
        utassert(r.rowReps == 1);
    }

    {
        // explicit_grid_sizing_mix_with_padding
        taffy::Style s;
        s.display = taffy::Display::Grid;
        s.size = SizeDim::FromLengths(120.0f, 120.0f);
        s.padding = {
            LengthPercentage::Length(10.0f), LengthPercentage::Length(10.0f),
            LengthPercentage::Length(20.0f), LengthPercentage::Length(20.0f)};
        TrackSizingFunction t = TrackSizingFunction::Length(20.0f);
        GridTemplateComponent c =
            GridRepeat(RepetitionCount::AutoFill(), &t, 1);
        GridTemplateComponent rr =
            GridRepeat(RepetitionCount::AutoFill(), &t, 1);
        s.gridTemplateColumns = GridTemplateOf(&c, 1);
        s.gridTemplateRows = GridTemplateOf(&rr, 1);
        ExplicitSizeResult r =
            RunExplicitSize(s, SizeFOpt{100.0f, 80.0f}, kMax);
        utassert(r.colCount == 5); // 40px horizontal padding
        utassert(r.rowCount == 4); // 40px vertical padding
        utassert(r.colReps == 5);
        utassert(r.rowReps == 4);
    }
}

static void TestInitializeGridTracks() {
    TestSuite("taffy::compute::grid::explicit_grid (initialize_grid_tracks)");

    CompactLength minpx0 = CompactLength::Length(0.0f);
    CompactLength minpx20 = CompactLength::Length(20.0f);
    CompactLength minpx100 = CompactLength::Length(100.0f);
    CompactLength autoLen = CompactLength::Auto();

    taffy::Style s;
    s.display = taffy::Display::Grid;
    s.gap = {LengthPercentage::Length(20.0f), LengthPercentage::Length(20.0f)};
    GridTemplateComponent cols[3] = {
        GridTemplateComponent::Single(TrackSizingFunction::Length(100.0f)),
        GridTemplateComponent::Single(
            TrackSizingFunction::MinMax(MinTrackSizingFunction::Length(100.0f),
                                        MaxTrackSizingFunction::Fr(2.0f))),
        GridTemplateComponent::Single(TrackSizingFunction::Fr(1.0f))};
    s.gridTemplateColumns = GridTemplateOf(cols, 3);
    TrackSizingFunction autoCols[2] = {TrackSizingFunction::Auto(),
                                       TrackSizingFunction::Length(100.0f)};
    s.gridAutoColumns = SliceDup(gGridArena, autoCols, 2);

    GridTrackForTest tracks[32];
    int n =
        GridInitTracksForTest(s, AbsoluteAxis::Horizontal, 3, 3, 3, tracks, 32);

    struct Expected {
        bool gutter;
        CompactLength min;
        CompactLength max;
    };
    Expected expected[] = {
        // Leading gutter
        {true, minpx0, minpx0},
        // Negative implicit tracks, cycling grid-auto-columns backwards
        {false, minpx100, minpx100},
        {true, minpx20, minpx20},
        {false, autoLen, autoLen},
        {true, minpx20, minpx20},
        {false, minpx100, minpx100},
        {true, minpx20, minpx20},
        // Explicit tracks
        {false, minpx100, minpx100},
        {true, minpx20, minpx20},
        // minmax() keeps its two halves apart
        {false, minpx100, CompactLength::Fr(2.0f)},
        {true, minpx20, minpx20},
        // An fr track's min sizing function is auto
        {false, autoLen, CompactLength::Fr(1.0f)},
        {true, minpx20, minpx20},
        // Positive implicit tracks
        {false, autoLen, autoLen},
        {true, minpx20, minpx20},
        {false, minpx100, minpx100},
        {true, minpx20, minpx20},
        {false, autoLen, autoLen},
        // Trailing gutter
        {true, minpx0, minpx0},
    };
    int expectedCount = (int)(sizeof(expected) / sizeof(expected[0]));
    utassert(n == expectedCount);
    for (int i = 0; i < n && i < expectedCount; i++) {
        utassert(tracks[i].isGutter == expected[i].gutter);
        utassert(tracks[i].min == expected[i].min);
        utassert(tracks[i].max == expected[i].max);
    }
}

// ─── src/compute/grid/implicit_grid.rs ───────────────────────────────────

static void TestGridImplicitSizing() {
    TestSuite("taffy::compute::grid::implicit_grid");

    {
        // child_min_max_line_auto
        int16_t minCol = 0;
        int16_t maxCol = 0;
        uint16_t span = 0;
        LinePlacement l = {GLine(5), GSpan(6)};
        GridChildMinMaxSpanForTest(l, 6, &minCol, &maxCol, &span);
        utassert(minCol == 4);
        utassert(maxCol == 10);
        utassert(span == 1);
    }

    {
        // child_min_max_line_negative_track
        int16_t minCol = 0;
        int16_t maxCol = 0;
        uint16_t span = 0;
        LinePlacement l = {GLine(-5), GSpan(3)};
        GridChildMinMaxSpanForTest(l, 6, &minCol, &maxCol, &span);
        utassert(minCol == 2);
        utassert(maxCol == 5);
        utassert(span == 1);
    }

    {
        // auto start, definite end: one track ending at that line. On an
        // empty grid that track is negative implicit, not a phantom
        // positive track after the end line.
        int16_t minCol = 0;
        int16_t maxCol = 0;
        uint16_t span = 0;
        LinePlacement l = {GAuto(), GLine(1)};
        GridChildMinMaxSpanForTest(l, 0, &minCol, &maxCol, &span);
        utassert(minCol == -1);
        utassert(maxCol == 0);
        utassert(span == 1);

        LinePlacement cols[1] = {{GAuto(), GLine(1)}};
        LinePlacement rows[1] = {{GLine(1), GLine(2)}};
        uint16_t colCounts[3];
        uint16_t rowCounts[3];
        GridSizeEstimateForTest(0, 1, cols, rows, 1, colCounts, rowCounts);
        utassert(colCounts[0] == 1);
        utassert(colCounts[1] == 0);
        utassert(colCounts[2] == 0);
        utassert(rowCounts[0] == 0);
        utassert(rowCounts[1] == 1);
        utassert(rowCounts[2] == 0);
    }

    {
        // explicit_grid_sizing_with_children
        LinePlacement cols[2] = {{GLine(1), GSpan(2)}, {GLine(-4), GAuto()}};
        LinePlacement rows[2] = {{GLine(2), GAuto()}, {GLine(-2), GAuto()}};
        uint16_t colCounts[3];
        uint16_t rowCounts[3];
        GridSizeEstimateForTest(6, 8, cols, rows, 2, colCounts, rowCounts);
        utassert(colCounts[0] == 0);
        utassert(colCounts[1] == 6);
        utassert(colCounts[2] == 0);
        utassert(rowCounts[0] == 0);
        utassert(rowCounts[1] == 8);
        utassert(rowCounts[2] == 0);
    }

    {
        // negative_implicit_grid_sizing
        LinePlacement cols[2] = {{GLine(-6), GSpan(2)}, {GLine(4), GAuto()}};
        LinePlacement rows[2] = {{GLine(-8), GAuto()}, {GLine(3), GAuto()}};
        uint16_t colCounts[3];
        uint16_t rowCounts[3];
        GridSizeEstimateForTest(4, 4, cols, rows, 2, colCounts, rowCounts);
        utassert(colCounts[0] == 1);
        utassert(colCounts[1] == 4);
        utassert(colCounts[2] == 0);
        utassert(rowCounts[0] == 3);
        utassert(rowCounts[1] == 4);
        utassert(rowCounts[2] == 0);
    }
}

// ─── src/compute/grid/placement.rs ───────────────────────────────────────

struct PlacementCase {
    taffy::Style style;
    GridPlacementForTest expected;
};

// `cases` is in source order — the order the children are added to the
// parent. The placement algorithm returns items in *placement* order: the
// items with a definite position in both axes first, then those definite only
// in the secondary axis, then the auto-placed ones. Where the two differ,
// `outputToCase` maps an output index back to its case; Rust's test runner
// does the same thing by sorting its children by node id before zipping.
static void RunPlacement(uint16_t explicitCols, uint16_t explicitRows,
                         const PlacementCase* cases, int n, GridAutoFlow flow,
                         const uint16_t* expectedCols,
                         const uint16_t* expectedRows,
                         const int* outputToCase = nullptr) {
    TaffyTree tree;
    tree.Init();
    TaffyNodeId kids[16];
    for (int i = 0; i < n; i++) {
        kids[i] = tree.NewLeaf(cases[i].style);
    }
    TaffyNodeId parent = tree.NewWithChildren(taffy::Style{}, kids, n);

    GridPlacementForTest out[16];
    uint16_t colCounts[3];
    uint16_t rowCounts[3];
    int placed = GridPlaceForTest(&tree, parent, explicitCols, explicitRows,
                                  flow, out, 16, colCounts, rowCounts);
    utassert(placed == n);
    for (int i = 0; i < n && i < placed; i++) {
        const GridPlacementForTest& want =
            cases[outputToCase ? outputToCase[i] : i].expected;
        utassert(out[i].columnStart == want.columnStart);
        utassert(out[i].columnEnd == want.columnEnd);
        utassert(out[i].rowStart == want.rowStart);
        utassert(out[i].rowEnd == want.rowEnd);
    }
    utassert(colCounts[0] == expectedCols[0]);
    utassert(colCounts[1] == expectedCols[1]);
    utassert(colCounts[2] == expectedCols[2]);
    utassert(rowCounts[0] == expectedRows[0]);
    utassert(rowCounts[1] == expectedRows[1]);
    utassert(rowCounts[2] == expectedRows[2]);
    tree.Free();
}

static void TestGridPlacement() {
    TestSuite("taffy::compute::grid::placement");

    {
        // test_only_fixed_placement
        PlacementCase cases[] = {
            {GridChild(GLine(1), GAuto(), GLine(1), GAuto()), {0, 1, 0, 1}},
            {GridChild(GLine(-4), GAuto(), GLine(-3), GAuto()), {-1, 0, 0, 1}},
            {GridChild(GLine(-3), GAuto(), GLine(-4), GAuto()), {0, 1, -1, 0}},
            {GridChild(GLine(3), GSpan(2), GLine(5), GAuto()), {2, 4, 4, 5}}};
        uint16_t cols[3] = {1, 2, 2};
        uint16_t rows[3] = {1, 2, 3};
        RunPlacement(2, 2, cases, 4, GridAutoFlow::Row, cols, rows);
    }

    {
        // test_placement_spanning_origin
        PlacementCase cases[] = {
            {GridChild(GLine(-1), GLine(-1), GLine(-1), GLine(-1)),
             {2, 3, 2, 3}},
            {GridChild(GLine(-1), GSpan(2), GLine(-1), GSpan(2)), {2, 4, 2, 4}},
            {GridChild(GLine(-4), GLine(-4), GLine(-4), GLine(-4)),
             {-1, 0, -1, 0}},
            {GridChild(GLine(-4), GSpan(2), GLine(-4), GSpan(2)),
             {-1, 1, -1, 1}}};
        uint16_t cols[3] = {1, 2, 2};
        uint16_t rows[3] = {1, 2, 2};
        RunPlacement(2, 2, cases, 4, GridAutoFlow::Row, cols, rows);
    }

    {
        // test_only_auto_placement_row_flow
        taffy::Style autoChild = GridChild(GAuto(), GAuto(), GAuto(), GAuto());
        PlacementCase cases[] = {
            {autoChild, {0, 1, 0, 1}}, {autoChild, {1, 2, 0, 1}},
            {autoChild, {0, 1, 1, 2}}, {autoChild, {1, 2, 1, 2}},
            {autoChild, {0, 1, 2, 3}}, {autoChild, {1, 2, 2, 3}},
            {autoChild, {0, 1, 3, 4}}, {autoChild, {1, 2, 3, 4}}};
        uint16_t cols[3] = {0, 2, 0};
        uint16_t rows[3] = {0, 2, 2};
        RunPlacement(2, 2, cases, 8, GridAutoFlow::Row, cols, rows);
    }

    {
        // test_only_auto_placement_column_flow
        taffy::Style autoChild = GridChild(GAuto(), GAuto(), GAuto(), GAuto());
        PlacementCase cases[] = {
            {autoChild, {0, 1, 0, 1}}, {autoChild, {0, 1, 1, 2}},
            {autoChild, {1, 2, 0, 1}}, {autoChild, {1, 2, 1, 2}},
            {autoChild, {2, 3, 0, 1}}, {autoChild, {2, 3, 1, 2}},
            {autoChild, {3, 4, 0, 1}}, {autoChild, {3, 4, 1, 2}}};
        uint16_t cols[3] = {0, 2, 2};
        uint16_t rows[3] = {0, 2, 0};
        RunPlacement(2, 2, cases, 8, GridAutoFlow::Column, cols, rows);
    }

    {
        // test_oversized_item
        PlacementCase cases[] = {
            {GridChild(GSpan(5), GAuto(), GAuto(), GAuto()), {0, 5, 0, 1}}};
        uint16_t cols[3] = {0, 2, 3};
        uint16_t rows[3] = {0, 2, 0};
        RunPlacement(2, 2, cases, 1, GridAutoFlow::Row, cols, rows);
    }

    {
        // test_fixed_in_secondary_axis
        PlacementCase cases[] = {
            {GridChild(GSpan(2), GAuto(), GLine(1), GAuto()), {0, 2, 0, 1}},
            {GridChild(GAuto(), GAuto(), GLine(2), GAuto()), {0, 1, 1, 2}},
            {GridChild(GAuto(), GAuto(), GLine(1), GAuto()), {2, 3, 0, 1}},
            {GridChild(GAuto(), GAuto(), GLine(4), GAuto()), {0, 1, 3, 4}}};
        uint16_t cols[3] = {0, 2, 1};
        uint16_t rows[3] = {0, 2, 2};
        RunPlacement(2, 2, cases, 4, GridAutoFlow::Row, cols, rows);
    }

    {
        // test_definite_in_secondary_axis_with_fully_definite_negative
        PlacementCase cases[] = {
            {GridChild(GAuto(), GAuto(), GLine(2), GAuto()), {0, 1, 1, 2}},
            {GridChild(GLine(-4), GAuto(), GLine(2), GAuto()), {-1, 0, 1, 2}},
            {GridChild(GAuto(), GAuto(), GLine(1), GAuto()), {-1, 0, 0, 1}}};
        uint16_t cols[3] = {1, 2, 0};
        uint16_t rows[3] = {0, 2, 0};
        // The second child is definite in both axes, so it is placed first.
        int order[3] = {1, 0, 2};
        RunPlacement(2, 2, cases, 3, GridAutoFlow::Row, cols, rows, order);
    }

    {
        // test_dense_packing_algorithm
        PlacementCase cases[] = {
            // Definitely positioned in column 2
            {GridChild(GLine(2), GAuto(), GLine(1), GAuto()), {1, 2, 0, 1}},
            // Spans 2 columns, so lands after the first item
            {GridChild(GSpan(2), GAuto(), GAuto(), GAuto()), {2, 4, 0, 1}},
            // Spans 1 column, so dense packing puts it before the first item
            {GridChild(GAuto(), GAuto(), GAuto(), GAuto()), {0, 1, 0, 1}}};
        uint16_t cols[3] = {0, 4, 0};
        uint16_t rows[3] = {0, 4, 0};
        RunPlacement(4, 4, cases, 3, GridAutoFlow::RowDense, cols, rows);
    }

    {
        // test_sparse_packing_algorithm
        PlacementCase cases[] = {
            {GridChild(GAuto(), GSpan(3), GAuto(), GAuto()), {0, 3, 0, 1}},
            // Width 3, so it wraps to the next row
            {GridChild(GAuto(), GSpan(3), GAuto(), GAuto()), {0, 3, 1, 2}},
            // Width 1, using the second row since the cursor is already there
            {GridChild(GAuto(), GSpan(1), GAuto(), GAuto()), {3, 4, 1, 2}}};
        uint16_t cols[3] = {0, 4, 0};
        uint16_t rows[3] = {0, 4, 0};
        RunPlacement(4, 4, cases, 3, GridAutoFlow::Row, cols, rows);
    }

    {
        // test_auto_placement_in_negative_tracks
        PlacementCase cases[] = {
            // Row 1, definitely positioned in column -2
            {GridChild(GLine(-5), GAuto(), GLine(1), GAuto()), {-2, -1, 0, 1}},
            // Row 2, auto positioned in column -2
            {GridChild(GAuto(), GAuto(), GLine(2), GAuto()), {-2, -1, 1, 2}},
            // Row 1, auto positioned in column -1
            {GridChild(GAuto(), GAuto(), GAuto(), GAuto()), {-1, 0, 0, 1}}};
        uint16_t cols[3] = {2, 2, 0};
        uint16_t rows[3] = {0, 2, 0};
        RunPlacement(2, 2, cases, 3, GridAutoFlow::RowDense, cols, rows);
    }
}

// ─── grid end-to-end ─────────────────────────────────────────────────────

// content_size_excludes_the_own_padding_of_a_non_scroll_container.
// A scroll container's content size includes its own end padding. A box
// that does not scroll reports only the content, whose start already
// includes the start padding.
static float ContentHeightFor(taffy::Display display, float top, float bottom,
                              bool scroll) {
    TaffyTree tree;
    tree.Init();
    taffy::Style childStyle;
    childStyle.size = SizeDim::FromLengths(100.0f, 1000.0f);
    TaffyNodeId child = tree.NewLeaf(childStyle);
    taffy::Style nodeStyle;
    nodeStyle.display = display;
    nodeStyle.boxSizing = taffy::BoxSizing::BorderBox;
    nodeStyle.size = SizeDim::FromLengths(300.0f, 200.0f);
    nodeStyle.padding.top = LengthPercentage::Length(top);
    nodeStyle.padding.bottom = LengthPercentage::Length(bottom);
    if (scroll) {
        nodeStyle.overflow = {taffy::Overflow::Scroll, taffy::Overflow::Scroll};
    }
    TaffyNodeId node = tree.NewWithChildren(nodeStyle, &child, 1);
    tree.ComputeLayout(node, SizeAvail::MaxContent());
    float h = tree.GetLayout(node).scrollableOverflowRect.bottom;
    tree.Free();
    return h;
}

// block_overflow_start_edge_absolute_child. A visible parent keeps
// start-side overflow in the rect. A scroll-container ancestor reports
// the reachable extent.
static void TestOverflowPastScrollOrigin() {
    TestSuite("taffy overflow past scroll origin");

    TaffyTree tree;
    tree.Init();
    taffy::Style absStyle;
    absStyle.display = taffy::Display::Block;
    absStyle.position = taffy::Position::Absolute;
    absStyle.size = SizeDim::FromLengths(80.0f, 40.0f);
    absStyle.inset.top = LengthPercentageAuto::Zero();
    absStyle.inset.right = LengthPercentageAuto::Zero();
    TaffyNodeId abs = tree.NewLeaf(absStyle);

    taffy::Style midStyle;
    midStyle.display = taffy::Display::Block;
    midStyle.position = taffy::Position::Relative;
    midStyle.size = SizeDim::FromLengths(40.0f, 20.0f);
    midStyle.margin.left = LengthPercentageAuto::Length(60.0f);
    TaffyNodeId mid = tree.NewWithChildren(midStyle, &abs, 1);

    taffy::Style rootStyle;
    rootStyle.display = taffy::Display::Block;
    rootStyle.overflow = {taffy::Overflow::Hidden, taffy::Overflow::Hidden};
    rootStyle.scrollbarWidth = 15.0f;
    rootStyle.size = SizeDim::FromLengths(100.0f, 100.0f);
    TaffyNodeId root = tree.NewWithChildren(rootStyle, &mid, 1);
    tree.ComputeLayout(root, SizeAvail::MaxContent());

    utassertnear(tree.GetLayout(abs).location.x, -40.0f);
    utassertnear(tree.GetLayout(abs).location.y, 0.0f);
    // mid is overflow:visible, so the child's start overflow is kept.
    utassertnear(tree.GetLayout(mid).scrollableOverflowRect.left, -40.0f);
    utassertnear(tree.GetLayout(mid).scrollableOverflowRect.right, 40.0f);
    utassertnear(tree.GetLayout(mid).location.x, 60.0f);
    // The stored rect contains the scroll origin, so a contribution that
    // starts at x=20 keeps left at 0. The reachable extent is 100.
    utassertnear(tree.GetLayout(root).scrollableOverflowRect.left, 0.0f);
    utassertnear(tree.GetLayout(root).scrollableOverflowRect.right, 100.0f);
    tree.Free();
}

// leaf_content_size_excludes_the_own_padding_of_a_non_scroll_container.
// explicit_min_main_size_skips_min_content_measurement. A flex item
// with a length min-width is not measured at min-content.
static taffy::LayoutOutput AssertNotMinContentMeasure(
    const taffy::LayoutInput& inputs, TaffyNodeId node, void* nodeContext,
    const taffy::Style* style, void* userData) {
    (void)node;
    (void)nodeContext;
    (void)style;
    (void)userData;
    utassert(inputs.availableSpace.width
                 .kind != taffy::AvailableSpace::Kind::MinContent);
    return taffy::LayoutOutput::FromOuterSize({10.0f, 10.0f});
}

static void TestExplicitMinSkipsMinContent() {
    TestSuite("taffy flex explicit min skips min-content");
    TaffyTree tree;
    tree.Init();
    taffy::Style childStyle;
    childStyle.minSize.width = taffy::Dimension::Length(10.0f);
    TaffyNodeId child = tree.NewLeaf(childStyle);
    TaffyNodeId root = tree.NewWithChildren(taffy::Style{}, &child, 1);
    tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                  AssertNotMinContentMeasure, nullptr);
    utassertnear(tree.GetLayout(child).size.w, 10.0f);
    tree.Free();
}

// An absolutely positioned child with both sizes set is laid out once.
// The extra min-content measurement is skipped.
static taffy::LayoutOutput CountCallsMeasure(const taffy::LayoutInput& inputs,
                                             TaffyNodeId node,
                                             void* nodeContext,
                                             const taffy::Style* style,
                                             void* userData) {
    (void)inputs;
    (void)node;
    (void)nodeContext;
    (void)style;
    if (userData) {
        (*(int*)userData)++;
    }
    return taffy::LayoutOutput::FromOuterSize({1.0f, 1.0f});
}

static void TestAbsoluteKnownSizeSkipsMeasure() {
    TestSuite("taffy absolute known size skips measure");

    int calls = 0;
    TaffyTree tree;
    tree.Init();
    taffy::Style absStyle;
    absStyle.position = taffy::Position::Absolute;
    absStyle.size = SizeDim::FromLengths(40.0f, 20.0f);
    TaffyNodeId abs = tree.NewLeaf(absStyle);
    taffy::Style blockStyle;
    blockStyle.display = taffy::Display::Block;
    blockStyle.size = SizeDim::FromLengths(100.0f, 100.0f);
    TaffyNodeId block = tree.NewWithChildren(blockStyle, &abs, 1);
    tree.ComputeLayoutWithMeasure(block, SizeAvail::MaxContent(),
                                  CountCallsMeasure, &calls);
    utassert(calls == 1);
    utassertnear(tree.GetLayout(abs).size.w, 40.0f);
    utassertnear(tree.GetLayout(abs).size.h, 20.0f);
    tree.Free();

    calls = 0;
    tree.Init();
    abs = tree.NewLeaf(absStyle);
    taffy::Style flexStyle;
    flexStyle.size = SizeDim::FromLengths(100.0f, 100.0f);
    TaffyNodeId flex = tree.NewWithChildren(flexStyle, &abs, 1);
    tree.ComputeLayoutWithMeasure(flex, SizeAvail::MaxContent(),
                                  CountCallsMeasure, &calls);
    utassert(calls == 1);
    utassertnear(tree.GetLayout(abs).size.w, 40.0f);
    tree.Free();
}

static void TestLeafContentSizeEndPadding() {
    TestSuite("taffy leaf content size end padding");
    SizeF measured = {100.0f, 1000.0f};
    auto heightFor = [&](float top, float bottom, bool scroll) {
        TaffyTree tree;
        tree.Init();
        taffy::Style style;
        style.boxSizing = taffy::BoxSizing::BorderBox;
        style.size = SizeDim::FromLengths(300.0f, 200.0f);
        style.padding.top = LengthPercentage::Length(top);
        style.padding.bottom = LengthPercentage::Length(bottom);
        if (scroll) {
            style.overflow = {taffy::Overflow::Scroll, taffy::Overflow::Scroll};
        }
        TaffyNodeId node = tree.NewLeafWithContext(style, &measured);
        tree.ComputeLayoutWithMeasure(node, SizeAvail::MaxContent(),
                                      SizeMeasureFunction, nullptr);
        float h = tree.GetLayout(node).scrollableOverflowRect.bottom;
        tree.Free();
        return h;
    };
    utassertnear(heightFor(20.0f, 0.0f, false), 1020.0f);
    utassertnear(heightFor(0.0f, 20.0f, false), 1000.0f);
    utassertnear(heightFor(20.0f, 20.0f, false), 1020.0f);
    utassertnear(heightFor(0.0f, 0.0f, false), 1000.0f);
    utassertnear(heightFor(20.0f, 0.0f, true), 1020.0f);
    utassertnear(heightFor(0.0f, 20.0f, true), 1020.0f);
    utassertnear(heightFor(20.0f, 20.0f, true), 1040.0f);
    utassertnear(heightFor(0.0f, 0.0f, true), 1000.0f);
}

static void TestContentSizeEndPadding() {
    TestSuite("taffy content size end padding");
    taffy::Display displays[] = {taffy::Display::Block, taffy::Display::Flex,
                                 taffy::Display::Grid};
    for (taffy::Display d : displays) {
        utassertnear(ContentHeightFor(d, 20.0f, 0.0f, false), 1020.0f);
        utassertnear(ContentHeightFor(d, 0.0f, 20.0f, false), 1000.0f);
        utassertnear(ContentHeightFor(d, 20.0f, 20.0f, false), 1020.0f);
        utassertnear(ContentHeightFor(d, 0.0f, 0.0f, false), 1000.0f);
        utassertnear(ContentHeightFor(d, 20.0f, 20.0f, true), 1040.0f);
        utassertnear(ContentHeightFor(d, 0.0f, 20.0f, true), 1020.0f);
    }
}

// scrollable_overflow.rs. The rect keeps start-side overflow. A scroll
// container drops a box that sits wholly before the scroll origin, and
// keeps the whole border box of one that only straddles it.
static taffy::Layout AbsoluteChildLayout(taffy::Display display,
                                         taffy::Overflow overflow,
                                         taffy::Direction direction,
                                         bool hasLeft, float insetLeft,
                                         bool hasRight, float insetRight) {
    constexpr float kContainer = 200.0f;
    constexpr float kChild = 50.0f;
    TaffyTree tree;
    tree.Init();
    taffy::Style childStyle;
    childStyle.position = taffy::Position::Absolute;
    childStyle.inset.left = hasLeft ? LengthPercentageAuto::Length(insetLeft)
                                    : LengthPercentageAuto::Auto();
    childStyle.inset.right = hasRight ? LengthPercentageAuto::Length(insetRight)
                                      : LengthPercentageAuto::Auto();
    childStyle.inset.top = LengthPercentageAuto::Zero();
    childStyle.inset.bottom = LengthPercentageAuto::Auto();
    childStyle.size = SizeDim::FromLengths(kChild, kChild);
    TaffyNodeId child = tree.NewLeaf(childStyle);
    taffy::Style nodeStyle;
    nodeStyle.display = display;
    nodeStyle.position = taffy::Position::Relative;
    nodeStyle.direction = direction;
    nodeStyle.size = SizeDim::FromLengths(kContainer, kContainer);
    nodeStyle.overflow = {overflow, overflow};
    TaffyNodeId node = tree.NewWithChildren(nodeStyle, &child, 1);
    tree.ComputeLayout(node, SizeAvail::MaxContent());
    taffy::Layout layout = tree.GetLayout(node);
    tree.Free();
    return layout;
}

static void TestScrollableOverflow() {
    TestSuite("taffy scrollable overflow");
    constexpr float kContainer = 200.0f;
    constexpr float kChild = 50.0f;
    taffy::Display displays[] = {taffy::Display::Block, taffy::Display::Flex,
                                 taffy::Display::Grid};
    for (taffy::Display d : displays) {
        taffy::Layout wholly = AbsoluteChildLayout(d, taffy::Overflow::Scroll,
                                                   taffy::Direction::Ltr, true,
                                                   -100.0f, false, 0.0f);
        utassertnear(wholly.scrollableOverflowRect.left, 0.0f);
        utassertnear(wholly.scrollableOverflowRect.right, 0.0f);
        utassertnear(wholly.scrollableOverflowRect.top, 0.0f);
        utassertnear(wholly.scrollableOverflowRect.bottom, 0.0f);
        utassertnear(wholly.ScrollWidth(), 0.0f);

        taffy::Layout visible = AbsoluteChildLayout(d, taffy::Overflow::Visible,
                                                    taffy::Direction::Ltr, true,
                                                    -100.0f, false, 0.0f);
        utassertnear(visible.scrollableOverflowRect.left, -100.0f);

        taffy::Layout partial = AbsoluteChildLayout(d, taffy::Overflow::Scroll,
                                                    taffy::Direction::Ltr, true,
                                                    -25.0f, false, 0.0f);
        utassertnear(partial.scrollableOverflowRect.left, -25.0f);
        utassertnear(partial.scrollableOverflowRect.right, 25.0f);
        utassertnear(partial.ScrollWidth(), 0.0f);

        taffy::Layout end = AbsoluteChildLayout(d, taffy::Overflow::Scroll,
                                                taffy::Direction::Ltr, true,
                                                kContainer, false, 0.0f);
        utassertnear(end.scrollableOverflowRect.right, kContainer + kChild);
        utassertnear(end.ScrollWidth(), kChild);

        TaffyTree tree;
        tree.Init();
        taffy::Style absStyle;
        absStyle.position = taffy::Position::Absolute;
        absStyle.inset.left = LengthPercentageAuto::Zero();
        absStyle.inset.right = LengthPercentageAuto::Auto();
        absStyle.inset.top = LengthPercentageAuto::Zero();
        absStyle.inset.bottom = LengthPercentageAuto::Auto();
        absStyle.size = SizeDim::FromLengths(5.0f, 5.0f);
        TaffyNodeId abs = tree.NewLeaf(absStyle);
        taffy::Style padStyle;
        padStyle.display = d;
        padStyle.position = taffy::Position::Relative;
        padStyle.overflow = {taffy::Overflow::Scroll, taffy::Overflow::Scroll};
        padStyle.padding.right = LengthPercentage::Length(20.0f);
        padStyle.padding.bottom = LengthPercentage::Length(30.0f);
        padStyle.size = SizeDim::FromLengths(kContainer, kContainer);
        TaffyNodeId padded = tree.NewWithChildren(padStyle, &abs, 1);
        tree.ComputeLayout(padded, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(padded).scrollableOverflowRect.right,
                     20.0f);
        utassertnear(tree.GetLayout(padded).scrollableOverflowRect.bottom,
                     30.0f);
        tree.Free();

        auto nested = [&](taffy::Overflow x, taffy::Overflow y) {
            TaffyTree nestedTree;
            nestedTree.Init();
            taffy::Style grandStyle;
            grandStyle.position = taffy::Position::Absolute;
            grandStyle.inset.left = LengthPercentageAuto::Zero();
            grandStyle.inset.right = LengthPercentageAuto::Auto();
            grandStyle.inset.top = LengthPercentageAuto::Length(kContainer);
            grandStyle.inset.bottom = LengthPercentageAuto::Auto();
            grandStyle.size = SizeDim::FromLengths(kChild, kChild);
            TaffyNodeId grand = nestedTree.NewLeaf(grandStyle);
            taffy::Style childStyle;
            childStyle.display = d;
            childStyle.position = taffy::Position::Relative;
            childStyle.size = SizeDim::FromLengths(kContainer, kContainer);
            childStyle.overflow = {x, y};
            TaffyNodeId child = nestedTree
                                    .NewWithChildren(childStyle, &grand, 1);
            taffy::Style rootStyle;
            rootStyle.display = d;
            rootStyle.size = SizeDim::FromLengths(kContainer, kContainer);
            TaffyNodeId root = nestedTree.NewWithChildren(rootStyle, &child, 1);
            nestedTree.ComputeLayout(root, SizeAvail::MaxContent());
            float bottom = nestedTree.GetLayout(root)
                               .scrollableOverflowRect.bottom;
            nestedTree.Free();
            return bottom;
        };
        utassertnear(nested(taffy::Overflow::Hidden, taffy::Overflow::Visible),
                     kContainer);
        utassertnear(nested(taffy::Overflow::Clip, taffy::Overflow::Visible),
                     kContainer + kChild);

        taffy::Layout rtlWholly = AbsoluteChildLayout(
            d, taffy::Overflow::Scroll, taffy::Direction::Rtl, false, 0.0f,
            true, -100.0f);
        utassertnear(rtlWholly.scrollableOverflowRect.left, 0.0f);
        utassertnear(rtlWholly.scrollableOverflowRect.right, 0.0f);
        utassertnear(rtlWholly.ScrollWidth(), 0.0f);

        taffy::Layout rtlEnd = AbsoluteChildLayout(d, taffy::Overflow::Scroll,
                                                   taffy::Direction::Rtl, true,
                                                   -kChild, false, 0.0f);
        utassertnear(rtlEnd.scrollableOverflowRect.right, kContainer + kChild);
        utassertnear(rtlEnd.ScrollWidth(), kChild);
    }
}

static void TestGridLayout() {
    TestSuite("taffy::compute::grid (layout)");

    {
        // A 2x2 grid of 1fr tracks splits a 200x100 container evenly.
        TaffyTree tree;
        tree.Init();
        TaffyNodeId kids[4];
        for (int i = 0; i < 4; i++) {
            kids[i] = tree.NewLeaf(taffy::Style{});
        }
        taffy::Style rootStyle = GridParent(200.0f, 100.0f, 2, 2);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 4);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        utassertnear(tree.GetLayout(kids[0]).location.x, 0.0f);
        utassertnear(tree.GetLayout(kids[0]).location.y, 0.0f);
        utassertnear(tree.GetLayout(kids[0]).size.w, 100.0f);
        utassertnear(tree.GetLayout(kids[0]).size.h, 50.0f);
        utassertnear(tree.GetLayout(kids[1]).location.x, 100.0f);
        utassertnear(tree.GetLayout(kids[1]).location.y, 0.0f);
        utassertnear(tree.GetLayout(kids[2]).location.x, 0.0f);
        utassertnear(tree.GetLayout(kids[2]).location.y, 50.0f);
        utassertnear(tree.GetLayout(kids[3]).location.x, 100.0f);
        utassertnear(tree.GetLayout(kids[3]).location.y, 50.0f);
        tree.Free();
    }

    {
        // Fixed columns with a gap, and a row sized by its content.
        TaffyTree tree;
        tree.Init();
        taffy::Style childStyle;
        childStyle.size = SizeDim::FromLengths(30.0f, 24.0f);
        TaffyNodeId c0 = tree.NewLeaf(childStyle);
        TaffyNodeId c1 = tree.NewLeaf(childStyle);
        TaffyNodeId kids[] = {c0, c1};

        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Grid;
        GridTemplateComponent cols[2] = {
            GridTemplateComponent::Single(TrackSizingFunction::Length(60.0f)),
            GridTemplateComponent::Single(TrackSizingFunction::Length(40.0f))};
        rootStyle.gridTemplateColumns = GridTemplateOf(cols, 2);
        rootStyle.gap = {LengthPercentage::Length(10.0f),
                         LengthPercentage::Length(0.0f)};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        // 60 + 10 + 40
        utassertnear(tree.GetLayout(root).size.w, 110.0f);
        utassertnear(tree.GetLayout(root).size.h, 24.0f);
        utassertnear(tree.GetLayout(c0).location.x, 0.0f);
        utassertnear(tree.GetLayout(c1).location.x, 70.0f);
        tree.Free();
    }

    {
        // grid-column: 1 / span 2 puts an item across both columns.
        TaffyTree tree;
        tree.Init();
        taffy::Style spanning = GridChild(GLine(1), GSpan(2), GAuto(), GAuto());
        spanning.display = taffy::Display::Flex;
        spanning.size.height = Dimension::Length(20.0f);
        TaffyNodeId wide = tree.NewLeaf(spanning);
        TaffyNodeId kids[] = {wide};
        taffy::Style rootStyle = GridParent(200.0f, 40.0f, 2, 1);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        utassertnear(tree.GetLayout(wide).location.x, 0.0f);
        utassertnear(tree.GetLayout(wide).size.w, 200.0f);
        tree.Free();
    }

    {
        // grid_align_items_baseline_overflow_scroll. A 20px scroll container
        // whose content is 40px has its baseline clamped to the border box,
        // so it shifts down 10px to meet a 60px sibling whose baseline is 30.
        TaffyTree tree;
        tree.Init();
        taffy::Style innerTall;
        innerTall.display = taffy::Display::Grid;
        innerTall.size = SizeDim::FromLengths(10.0f, 40.0f);
        TaffyNodeId tall = tree.NewLeaf(innerTall);

        taffy::Style scrollerStyle;
        scrollerStyle.display = taffy::Display::Grid;
        scrollerStyle
            .overflow = {taffy::Overflow::Scroll, taffy::Overflow::Scroll};
        scrollerStyle.scrollbarWidth = 15.0f;
        scrollerStyle.size = SizeDim::FromLengths(60.0f, 20.0f);
        TaffyNodeId scroller = tree.NewWithChildren(scrollerStyle, &tall, 1);

        taffy::Style innerShort;
        innerShort.display = taffy::Display::Grid;
        innerShort.size = SizeDim::FromLengths(60.0f, 30.0f);
        TaffyNodeId shortChild = tree.NewLeaf(innerShort);

        taffy::Style siblingStyle;
        siblingStyle.display = taffy::Display::Grid;
        siblingStyle.size = SizeDim::FromLengths(60.0f, 60.0f);
        TaffyNodeId sibling =
            tree.NewWithChildren(siblingStyle, &shortChild, 1);

        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Grid;
        rootStyle.alignItems = AlignItems{AlignItemsKeyword::Baseline};
        rootStyle.size = SizeDim::FromLengths(120.0f, 100.0f);
        GridTemplateComponent cols[2] = {
            GridTemplateComponent::Single(TrackSizingFunction::Length(60.0f)),
            GridTemplateComponent::Single(TrackSizingFunction::Length(60.0f))};
        rootStyle.gridTemplateColumns = GridTemplateOf(cols, 2);
        TaffyNodeId kids[] = {scroller, sibling};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayout(root, SizeAvail::MaxContent());

        const Layout& scrolled = tree.GetLayout(scroller);
        utassertnear(scrolled.location.x, 0.0f);
        utassertnear(scrolled.location.y, 10.0f);
        utassertnear(scrolled.size.w, 60.0f);
        utassertnear(scrolled.size.h, 20.0f);
        const Layout& sib = tree.GetLayout(sibling);
        utassertnear(sib.location.x, 60.0f);
        utassertnear(sib.location.y, 0.0f);
        utassertnear(sib.size.w, 60.0f);
        utassertnear(sib.size.h, 60.0f);
        utassertnear(tree.GetLayout(tall).location.x, 0.0f);
        utassertnear(tree.GetLayout(tall).location.y, 0.0f);
        utassertnear(tree.GetLayout(tall).size.w, 10.0f);
        utassertnear(tree.GetLayout(tall).size.h, 40.0f);
        utassertnear(tree.GetLayout(shortChild).size.h, 30.0f);
        tree.Free();
    }
}

// grid_align_items_baseline_child_auto_margin. An auto block-axis margin
// opts the item out of baseline alignment, so it is not shimmed.
// CSS Grid 2 §12.5. A flexible track receives only its flex-factor
// share of an item's content, clamped at one. 0fr with min-height:0
// collapses; a definite size still sizes a 0fr track.
static void TestGridFrContentProportion() {
    TestSuite("taffy grid fr content proportion");

    auto rowsOf = [](const TrackSizingFunction* rows, int nRows) {
        GridTemplateComponent comps[2];
        for (int i = 0; i < nRows; i++) {
            comps[i] = GridTemplateComponent::Single(rows[i]);
        }
        return GridTemplateOf(comps, nRows);
    };
    GridTemplateComponent col =
        GridTemplateComponent::Single(TrackSizingFunction::Length(20.0f));

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style content;
        content.size = SizeDim::FromLengths(20.0f, 100.0f);
        TaffyNodeId contentNode = tree.NewLeaf(content);
        taffy::Style item;
        item.minSize.height = Dimension::Length(0.0f);
        TaffyNodeId itemNode = tree.NewWithChildren(item, &contentNode, 1);
        taffy::Style gridStyle;
        gridStyle.display = taffy::Display::Grid;
        TrackSizingFunction row = TrackSizingFunction::Fr(0.0f);
        gridStyle.gridTemplateRows = rowsOf(&row, 1);
        gridStyle.gridTemplateColumns = GridTemplateOf(&col, 1);
        TaffyNodeId grid = tree.NewWithChildren(gridStyle, &itemNode, 1);
        tree.ComputeLayout(grid, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(grid).size.w, 20.0f);
        utassertnear(tree.GetLayout(grid).size.h, 0.0f);
        utassertnear(tree.GetLayout(itemNode).size.h, 0.0f);
        utassertnear(tree.GetLayout(contentNode).size.h, 100.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style content;
        content.size = SizeDim::FromLengths(20.0f, 100.0f);
        TaffyNodeId contentNode = tree.NewLeaf(content);
        taffy::Style item;
        item.minSize.height = Dimension::Length(0.0f);
        TaffyNodeId itemNode = tree.NewWithChildren(item, &contentNode, 1);
        taffy::Style gridStyle;
        gridStyle.display = taffy::Display::Grid;
        TrackSizingFunction row = TrackSizingFunction::Fr(2.0f);
        gridStyle.gridTemplateRows = rowsOf(&row, 1);
        gridStyle.gridTemplateColumns = GridTemplateOf(&col, 1);
        TaffyNodeId grid = tree.NewWithChildren(gridStyle, &itemNode, 1);
        tree.ComputeLayout(grid, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(grid).size.h, 100.0f);
        utassertnear(tree.GetLayout(itemNode).size.h, 100.0f);
        utassertnear(tree.GetLayout(contentNode).size.h, 100.0f);
        tree.Free();
    }

    {
        // The flex parent gives the grid a definite height on the second
        // pass, so the 0.5fr row is half of the 100px content.
        TaffyTree tree;
        tree.Init();
        taffy::Style content;
        content.size = SizeDim::FromLengths(20.0f, 100.0f);
        TaffyNodeId contentNode = tree.NewLeaf(content);
        taffy::Style item;
        item.minSize.height = Dimension::Length(0.0f);
        TaffyNodeId itemNode = tree.NewWithChildren(item, &contentNode, 1);
        taffy::Style gridStyle;
        gridStyle.display = taffy::Display::Grid;
        TrackSizingFunction row = TrackSizingFunction::Fr(0.5f);
        gridStyle.gridTemplateRows = rowsOf(&row, 1);
        gridStyle.gridTemplateColumns = GridTemplateOf(&col, 1);
        TaffyNodeId grid = tree.NewWithChildren(gridStyle, &itemNode, 1);
        taffy::Style flexStyle;
        flexStyle.alignItems = AlignItems{AlignItemsKeyword::Start};
        flexStyle.size = SizeDim::FromLengths(20.0f, 150.0f);
        TaffyNodeId flex = tree.NewWithChildren(flexStyle, &grid, 1);
        tree.ComputeLayout(flex, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(flex).size.h, 150.0f);
        utassertnear(tree.GetLayout(grid).size.h, 50.0f);
        utassertnear(tree.GetLayout(itemNode).size.h, 25.0f);
        utassertnear(tree.GetLayout(contentNode).size.h, 100.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style item;
        item.size.height = Dimension::Length(100.0f);
        item.gridRow = {GLine(1), GLine(2)};
        TaffyNodeId itemNode = tree.NewLeaf(item);
        taffy::Style gridStyle;
        gridStyle.display = taffy::Display::Grid;
        TrackSizingFunction rows[2] = {TrackSizingFunction::Fr(0.0f),
                                       TrackSizingFunction::Fr(1.0f)};
        gridStyle.gridTemplateRows = rowsOf(rows, 2);
        gridStyle.gridTemplateColumns = GridTemplateOf(&col, 1);
        TaffyNodeId grid = tree.NewWithChildren(gridStyle, &itemNode, 1);
        tree.ComputeLayout(grid, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(grid).size.h, 200.0f);
        utassertnear(tree.GetLayout(itemNode).size.h, 100.0f);
        tree.Free();
    }
}

static void TestGridAutoMarginBaseline() {
    TestSuite("taffy grid auto margin baseline");

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style a;
        a.display = taffy::Display::Grid;
        a.size.height = Dimension::Length(20.0f);
        taffy::Style b;
        b.display = taffy::Display::Grid;
        b.size.height = Dimension::Length(30.0f);
        taffy::Style inner;
        inner.display = taffy::Display::Grid;
        inner.alignSelf = OptAlignSelf(AlignItems{AlignItemsKeyword::Start});
        inner.size.height = Dimension::Length(5.0f);
        TaffyNodeId innerNode = tree.NewLeaf(inner);
        taffy::Style c;
        c.display = taffy::Display::Grid;
        c.size.height = Dimension::Length(25.0f);
        c.margin.top = LengthPercentageAuto::Auto();
        TaffyNodeId cNode = tree.NewWithChildren(c, &innerNode, 1);
        TaffyNodeId aNode = tree.NewLeaf(a);
        TaffyNodeId bNode = tree.NewLeaf(b);
        TaffyNodeId kids[] = {aNode, bNode, cNode};
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Grid;
        rootStyle.alignItems = AlignItems{AlignItemsKeyword::Baseline};
        rootStyle.size.width = Dimension::Length(120.0f);
        rootStyle.gridAutoFlow = taffy::GridAutoFlow::Column;
        TrackSizingFunction col = TrackSizingFunction::Length(40.0f);
        rootStyle.gridAutoColumns = SliceDup(gGridArena, &col, 1);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 3);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(root).size.w, 120.0f);
        utassertnear(tree.GetLayout(root).size.h, 30.0f);
        utassertnear(tree.GetLayout(aNode).location.y, 10.0f);
        utassertnear(tree.GetLayout(bNode).location.y, 0.0f);
        utassertnear(tree.GetLayout(cNode).location.x, 80.0f);
        utassertnear(tree.GetLayout(cNode).location.y, 5.0f);
        utassertnear(tree.GetLayout(cNode).size.h, 25.0f);
        utassertnear(tree.GetLayout(innerNode).location.y, 0.0f);
        tree.Free();
    }

    {
        // The auto-margin item does not supply the container baseline, so
        // the outer row aligns to the nested grid's first item.
        TaffyTree tree;
        tree.Init();
        taffy::Style first;
        first.display = taffy::Display::Grid;
        first.size = SizeDim::FromLengths(50.0f, 30.0f);
        TaffyNodeId firstNode = tree.NewLeaf(first);

        taffy::Style innerA;
        innerA.display = taffy::Display::Grid;
        innerA.size.height = Dimension::Length(20.0f);
        TaffyNodeId innerANode = tree.NewLeaf(innerA);
        taffy::Style innerB;
        innerB.display = taffy::Display::Grid;
        innerB
            .alignSelf = OptAlignSelf(AlignItems{AlignItemsKeyword::Baseline});
        innerB.size.height = Dimension::Length(10.0f);
        innerB.margin.top = LengthPercentageAuto::Auto();
        TaffyNodeId innerBNode = tree.NewLeaf(innerB);
        TaffyNodeId innerKids[] = {innerANode, innerBNode};
        taffy::Style nested;
        nested.display = taffy::Display::Grid;
        nested.size.width = Dimension::Length(50.0f);
        nested.gridAutoFlow = taffy::GridAutoFlow::Column;
        TrackSizingFunction autoRow = TrackSizingFunction::Length(40.0f);
        TrackSizingFunction autoCol = TrackSizingFunction::Length(25.0f);
        nested.gridAutoRows = SliceDup(gGridArena, &autoRow, 1);
        nested.gridAutoColumns = SliceDup(gGridArena, &autoCol, 1);
        TaffyNodeId nestedNode = tree.NewWithChildren(nested, innerKids, 2);

        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Grid;
        rootStyle.alignItems = AlignItems{AlignItemsKeyword::Baseline};
        rootStyle.size = SizeDim::FromLengths(120.0f, 80.0f);
        GridTemplateComponent cols[2] = {
            GridTemplateComponent::Single(TrackSizingFunction::Length(60.0f)),
            GridTemplateComponent::Single(TrackSizingFunction::Length(60.0f))};
        rootStyle.gridTemplateColumns = GridTemplateOf(cols, 2);
        TaffyNodeId kids[] = {firstNode, nestedNode};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(firstNode).location.y, 0.0f);
        utassertnear(tree.GetLayout(firstNode).size.h, 30.0f);
        utassertnear(tree.GetLayout(nestedNode).location.x, 60.0f);
        utassertnear(tree.GetLayout(nestedNode).location.y, 10.0f);
        utassertnear(tree.GetLayout(nestedNode).size.h, 40.0f);
        utassertnear(tree.GetLayout(innerANode).location.y, 0.0f);
        utassertnear(tree.GetLayout(innerANode).size.h, 20.0f);
        utassertnear(tree.GetLayout(innerBNode).location.x, 25.0f);
        utassertnear(tree.GetLayout(innerBNode).location.y, 30.0f);
        utassertnear(tree.GetLayout(innerBNode).size.h, 10.0f);
        tree.Free();
    }

    {
        // A fixed-height item shifts to share the row's baseline. A
        // percentage height on the same intrinsic row is cyclic, so it
        // stays at the start edge and does not push its sibling.
        auto row = [](Dimension secondHeight, float firstY, float secondY) {
            TaffyTree tree;
            tree.Init();
            taffy::Style first;
            first.size = SizeDim::FromLengths(40.0f, 30.0f);
            TaffyNodeId firstNode = tree.NewLeaf(first);
            taffy::Style second;
            second.size.width = Dimension::Length(40.0f);
            second.size.height = secondHeight;
            TaffyNodeId secondNode = tree.NewLeaf(second);
            taffy::Style rootStyle;
            rootStyle.display = taffy::Display::Grid;
            rootStyle.alignItems = AlignItems{AlignItemsKeyword::Baseline};
            TrackSizingFunction col = TrackSizingFunction::Length(40.0f);
            GridTemplateComponent cols[2] = {
                GridTemplateComponent::Single(col),
                GridTemplateComponent::Single(col)};
            rootStyle.gridTemplateColumns = GridTemplateOf(cols, 2);
            TaffyNodeId kids[] = {firstNode, secondNode};
            TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
            tree.ComputeLayout(root, SizeAvail::MaxContent());
            utassertnear(tree.GetLayout(firstNode).location.y, firstY);
            utassertnear(tree.GetLayout(secondNode).location.y, secondY);
            tree.Free();
        };
        row(Dimension::Length(10.0f), 0.0f, 20.0f);
        row(Dimension::Percent(0.5f), 0.0f, 0.0f);
    }
}

// A scroll container holding a column of fixed-height children: the column
// overflows rather than shrinking, because its own automatic minimum size is
// its min-content height, and that is the sum of three definite heights.
// The numbers are what the Rust crate prints for the same tree.
static void TestScrollColumnDoesNotShrink() {
    TestSuite("taffy scroll column");

    TaffyTree tree;
    tree.Init();

    // Each filler centres a line of text, so it has content of its own that
    // is far shorter than the height it asks for.
    taffy::Style textStyle;
    textStyle.size = SizeDim::FromLengths(149.0f, 22.0f);

    taffy::Style leaf;
    leaf.size = {taffy::Dimension::Percent(1.0f),
                 taffy::Dimension::Length(400.0f)};
    leaf.alignItems = taffy::AlignItems{taffy::AlignItemsKeyword::Center};
    leaf.justifyContent =
        taffy::AlignContent{taffy::AlignContentKeyword::Center};
    TaffyNodeId ta = tree.NewLeaf(textStyle);
    TaffyNodeId a = tree.NewWithChildren(leaf, &ta, 1);
    leaf.size.height = taffy::Dimension::Length(300.0f);
    TaffyNodeId tb = tree.NewLeaf(textStyle);
    TaffyNodeId b = tree.NewWithChildren(leaf, &tb, 1);
    leaf.size.height = taffy::Dimension::Length(800.0f);
    TaffyNodeId tc = tree.NewLeaf(textStyle);
    TaffyNodeId c = tree.NewWithChildren(leaf, &tc, 1);

    taffy::Style colStyle;
    colStyle.flexDirection = FlexDirection::Column;
    colStyle.padding = {taffy::LengthPercentage::Length(16.0f),
                        taffy::LengthPercentage::Length(16.0f),
                        taffy::LengthPercentage::Length(16.0f),
                        taffy::LengthPercentage::Length(16.0f)};
    colStyle.gap = {taffy::LengthPercentage::Length(16.0f),
                    taffy::LengthPercentage::Length(16.0f)};
    TaffyNodeId kids[3] = {a, b, c};
    TaffyNodeId col = tree.NewWithChildren(colStyle, kids, 3);

    taffy::Style pageStyle;
    pageStyle.flexDirection = FlexDirection::Column;
    pageStyle.size = {taffy::Dimension::Percent(1.0f),
                      taffy::Dimension::Percent(1.0f)};
    pageStyle.overflow = {taffy::Overflow::Visible, taffy::Overflow::Scroll};
    TaffyNodeId page = tree.NewWithChildren(pageStyle, &col, 1);

    // The scroll container is itself the node layout runs on, which is what
    // `LayoutEl` does with the element tree's root.
    // The example hangs its scrollbar thumb off the scroll container.
    taffy::Style thumbStyle;
    thumbStyle.position = taffy::Position::Absolute;
    thumbStyle.size = SizeDim::FromLengths(6.0f, 80.0f);
    tree.AddChild(page, tree.NewLeaf(thumbStyle));

    TaffyNodeId root = page;

    tree.ComputeLayout(root, SizeAvail::Definite(SizeF{700.0f, 700.0f}));

    utassertnear(tree.GetLayout(page).size.w, 700.0f);
    utassertnear(tree.GetLayout(col).size.w, 700.0f);
    utassertnear(tree.GetLayout(a).size.w, 668.0f);
    utassertnear(tree.GetLayout(page).size.h, 700.0f);
    utassertnear(tree.GetLayout(col).size.h, 1564.0f);
    utassertnear(tree.GetLayout(a).size.h, 400.0f);
    utassertnear(tree.GetLayout(b).size.h, 300.0f);
    utassertnear(tree.GetLayout(c).size.h, 800.0f);
    utassertnear(tree.GetLayout(b).location.y, 432.0f);

    tree.Free();
}

static void TestTaffy013Regressions() {
    TestSuite("taffy 0.13 regressions");

    // display:flow-root contains its floats.
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style floated;
        floated.display = taffy::Display::Block;
        floated.floatMode = taffy::Float::Left;
        floated.size = SizeDim::FromLengths(20.0f, 30.0f);
        TaffyNodeId child = tree.NewLeaf(floated);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::FlowRoot;
        rootStyle.size.width = Dimension::Length(100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(root).size.w, 100.0f);
        utassertnear(tree.GetLayout(root).size.h, 30.0f);
        tree.Free();
    }

    // A flow-root beside a preceding float establishes an independent BFC
    // and narrows to the remaining width.
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style floated;
        floated.display = taffy::Display::Block;
        floated.floatMode = taffy::Float::Left;
        floated.size = SizeDim::FromLengths(30.0f, 20.0f);
        TaffyNodeId a = tree.NewLeaf(floated);
        taffy::Style innerStyle;
        innerStyle.display = taffy::Display::Block;
        innerStyle.size.height = Dimension::Length(10.0f);
        TaffyNodeId inner = tree.NewLeaf(innerStyle);
        taffy::Style flow;
        flow.display = taffy::Display::FlowRoot;
        TaffyNodeId b = tree.NewWithChildren(flow, &inner, 1);
        TaffyNodeId kids[] = {a, b};
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.size.width = Dimension::Length(100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(root).size.h, 20.0f);
        utassertnear(tree.GetLayout(b).location.x, 30.0f);
        utassertnear(tree.GetLayout(b).size.w, 70.0f);
        utassertnear(tree.GetLayout(inner).size.w, 70.0f);
        tree.Free();
    }

    // self-start in a flex column uses the child's own inline direction.
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style childStyle;
        childStyle.direction = taffy::Direction::Rtl;
        childStyle.size = SizeDim::FromLengths(10.0f, 10.0f);
        childStyle
            .alignSelf = OptAlignSelf(AlignItems{AlignItemsKeyword::SelfStart});
        TaffyNodeId child = tree.NewLeaf(childStyle);
        taffy::Style rootStyle;
        rootStyle.flexDirection = FlexDirection::Column;
        rootStyle.size = SizeDim::FromLengths(100.0f, 20.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(child).location.x, 90.0f);
        tree.Free();
    }

    // The scroll extent subtracts the padding box, including both border
    // edges symmetrically.
    {
        Layout l;
        l.size = {100.0f, 80.0f};
        l.scrollableOverflowRect = {0.0f, 120.0f, 0.0f, 95.0f};
        l.border = {3.0f, 7.0f, 5.0f, 11.0f};
        utassertnear(l.ScrollWidth(), 30.0f);
        utassertnear(l.ScrollHeight(), 31.0f);
    }

    // Overlarge explicit grids are clamped to the CSS-mandated 10,000
    // tracks, including fixed repetitions.
    {
        TrackSizingFunction track = TrackSizingFunction::Auto();
        Slice<TrackSizingFunction> tracks = SliceDup(gGridArena, &track, 1);
        GridTemplateRepetition rep;
        rep.count = RepetitionCount::Exactly(20000);
        rep.tracks = tracks;
        GridTemplateComponent component = GridTemplateComponent::Repeat(rep);
        taffy::Style style;
        style.gridTemplateColumns = GridTemplateOf(&component, 1);
        uint16_t repeats = 0;
        uint16_t count = 0;
        GridExplicitSizeForTest(style, None(), true, AbsoluteAxis::Horizontal,
                                NoCalc(), &repeats, &count);
        utassert(count == 10000);
    }
}

// Layout and paint containment. Both establish an independent formatting
// context and contain scrollable overflow. Only layout suppresses baselines.
// Size and inline-size containment are not part of this commit.
static void TestContainment() {
    TestSuite("taffy contain");

    utassert(Contain::Layout().SuppressesBaseline());
    utassert(!Contain::Paint().SuppressesBaseline());
    utassert(Contain::Content().SuppressesBaseline());
    utassert(Contain::Layout().EstablishesIndependentFormattingContext());
    utassert(Contain::Paint().EstablishesIndependentFormattingContext());
    utassert(!Contain::None().EstablishesIndependentFormattingContext());
    utassert(Contain::Layout().ContainsScrollableOverflow());
    utassert(Contain::Paint().ContainsScrollableOverflow());
    utassert(!Contain::None().ContainsScrollableOverflow());
    utassert(Contain::Content() == Contain::Layout().Union(Contain::Paint()));
    taffy::Style plain;
    taffy::Style contained = plain;
    utassert(plain == contained);
    contained.contain = Contain::Paint();
    utassert(!(plain == contained));

    auto marginCollapse = [](Contain contain, float midH, float innerY) {
        TaffyTree tree;
        tree.Init();
        taffy::Style innerStyle;
        innerStyle.display = taffy::Display::Block;
        innerStyle.size.height = Dimension::Length(10.0f);
        innerStyle.margin.top = LengthPercentageAuto::Length(20.0f);
        TaffyNodeId inner = tree.NewLeaf(innerStyle);

        taffy::Style midStyle;
        midStyle.display = taffy::Display::Block;
        midStyle.contain = contain;
        TaffyNodeId mid = tree.NewWithChildren(midStyle, &inner, 1);

        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.size.width = Dimension::Length(100.0f);
        rootStyle.border = {
            LengthPercentage::Length(1.0f), LengthPercentage::Length(1.0f),
            LengthPercentage::Length(1.0f), LengthPercentage::Length(1.0f)};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &mid, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(root).size.h, 32.0f);
        utassertnear(tree.GetLayout(mid).size.h, midH);
        utassertnear(tree.GetLayout(inner).location.y, innerY);
        tree.Free();
    };
    // No containment: the 20px margin collapses out of the middle box.
    marginCollapse(Contain::None(), 10.0f, 0.0f);
    // Paint and layout both stop that collapse. The margin stays inside.
    marginCollapse(Contain::Paint(), 30.0f, 20.0f);
    marginCollapse(Contain::Layout(), 30.0f, 20.0f);

    // contain_layout_block_contains_floats. The contained block keeps the
    // float, so the following sibling starts below it. Without containment
    // the float escapes into the root and the sibling overlaps it.
    auto containsFloat = [](Contain contain, float rootH, float midH,
                            float tailY) {
        TaffyTree tree;
        tree.Init();
        taffy::Style floated;
        floated.display = taffy::Display::Block;
        floated.floatMode = taffy::Float::Left;
        floated.size = SizeDim::FromLengths(30.0f, 30.0f);
        TaffyNodeId child = tree.NewLeaf(floated);
        taffy::Style midStyle;
        midStyle.display = taffy::Display::Block;
        midStyle.contain = contain;
        TaffyNodeId mid = tree.NewWithChildren(midStyle, &child, 1);
        taffy::Style tailStyle;
        tailStyle.display = taffy::Display::Block;
        tailStyle.size.height = Dimension::Length(10.0f);
        TaffyNodeId tail = tree.NewLeaf(tailStyle);
        TaffyNodeId kids[] = {mid, tail};
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.size.width = Dimension::Length(100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(root).size.h, rootH);
        utassertnear(tree.GetLayout(mid).size.h, midH);
        utassertnear(tree.GetLayout(child).location.y, 0.0f);
        utassertnear(tree.GetLayout(child).size.w, 30.0f);
        utassertnear(tree.GetLayout(tail).location.y, tailY);
        tree.Free();
    };
    containsFloat(Contain::None(), 30.0f, 0.0f, 0.0f);
    containsFloat(Contain::Layout(), 40.0f, 30.0f, 30.0f);
    containsFloat(Contain::Paint(), 40.0f, 30.0f, 30.0f);

    {
        // A contained block beside a float is its own BFC, so it narrows
        // to the space the float leaves rather than running under it.
        TaffyTree tree;
        tree.Init();
        taffy::Style floated;
        floated.display = taffy::Display::Block;
        floated.floatMode = taffy::Float::Left;
        floated.size = SizeDim::FromLengths(30.0f, 20.0f);
        TaffyNodeId a = tree.NewLeaf(floated);
        taffy::Style innerStyle;
        innerStyle.display = taffy::Display::Block;
        innerStyle.size.height = Dimension::Length(10.0f);
        TaffyNodeId inner = tree.NewLeaf(innerStyle);
        taffy::Style box;
        box.display = taffy::Display::Block;
        box.contain = Contain::Paint();
        TaffyNodeId b = tree.NewWithChildren(box, &inner, 1);
        TaffyNodeId kids[] = {a, b};
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.size.width = Dimension::Length(100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(root).size.h, 20.0f);
        utassertnear(tree.GetLayout(b).location.x, 30.0f);
        utassertnear(tree.GetLayout(b).size.w, 70.0f);
        utassertnear(tree.GetLayout(inner).size.w, 70.0f);
        tree.Free();
    }

    auto baselineShift = [](taffy::Display display, Contain contain,
                            float siblingY) {
        TaffyTree tree;
        tree.Init();
        BaselineContext content = {{30.0f, 30.0f}, Some(10.0f)};
        taffy::Style contentStyle;
        contentStyle.display = taffy::Display::Block;
        contentStyle.size = SizeDim::FromLengths(30.0f, 30.0f);
        TaffyNodeId contentNode =
            tree.NewLeafWithContext(contentStyle, &content);
        taffy::Style box;
        box.display = display;
        box.contain = contain;
        box.size.width = Dimension::Length(40.0f);
        TaffyNodeId itemA = tree.NewWithChildren(box, &contentNode, 1);
        BaselineContext refCtx = {{40.0f, 80.0f}, Some(10.0f)};
        TaffyNodeId itemB = tree.NewLeafWithContext(taffy::Style{}, &refCtx);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Flex;
        rootStyle.flexDirection = FlexDirection::Row;
        rootStyle.alignItems = AlignItems{AlignItemsKeyword::Baseline};
        rootStyle.size = SizeDim::FromLengths(200.0f, 120.0f);
        TaffyNodeId kids[] = {itemA, itemB};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      BaselineMeasure, nullptr);
        utassertnear(tree.GetLayout(itemA).location.y, 0.0f);
        utassertnear(tree.GetLayout(itemB).location.y, siblingY);
        tree.Free();
    };
    // Paint keeps the content baseline (10). Layout drops it, so the flex
    // line uses the box's bottom edge (30) and the sibling shifts by 20.
    baselineShift(taffy::Display::Block, Contain::None(), 0.0f);
    baselineShift(taffy::Display::Block, Contain::Paint(), 0.0f);
    baselineShift(taffy::Display::Block, Contain::Layout(), 20.0f);
    baselineShift(taffy::Display::Block, Contain::Content(), 20.0f);
    baselineShift(taffy::Display::Flex, Contain::Layout(), 20.0f);
    baselineShift(taffy::Display::Flex, Contain::Paint(), 0.0f);

    // contain_layout_flex_baseline / contain_paint_flex_baseline. The first
    // item's baseline is its 30px child. Layout containment drops the second
    // item's baseline, so it aligns its bottom (20) to that 30. Paint keeps
    // the 10px child's baseline, so the item shifts down by 20.
    auto flexBaseline = [](Contain contain, float secondY) {
        TaffyTree tree;
        tree.Init();
        taffy::Style tallChild;
        tallChild.size = SizeDim::FromLengths(50.0f, 30.0f);
        TaffyNodeId tall = tree.NewLeaf(tallChild);
        taffy::Style firstStyle;
        firstStyle.size = SizeDim::FromLengths(50.0f, 60.0f);
        TaffyNodeId first = tree.NewWithChildren(firstStyle, &tall, 1);
        taffy::Style shortChild;
        shortChild.size = SizeDim::FromLengths(50.0f, 10.0f);
        TaffyNodeId shortNode = tree.NewLeaf(shortChild);
        taffy::Style secondStyle;
        secondStyle.contain = contain;
        secondStyle.size = SizeDim::FromLengths(50.0f, 20.0f);
        TaffyNodeId second = tree.NewWithChildren(secondStyle, &shortNode, 1);
        taffy::Style rootStyle;
        rootStyle.alignItems = AlignItems{AlignItemsKeyword::Baseline};
        rootStyle.size.width = Dimension::Length(200.0f);
        TaffyNodeId kids[] = {first, second};
        TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(root).size.h, 60.0f);
        utassertnear(tree.GetLayout(first).location.y, 0.0f);
        utassertnear(tree.GetLayout(second).location.y, secondY);
        utassertnear(tree.GetLayout(shortNode).location.y, 0.0f);
        tree.Free();
    };
    flexBaseline(Contain::Layout(), 10.0f);
    flexBaseline(Contain::Paint(), 20.0f);

    // A grid records a content baseline only once a row has two
    // baseline-aligned items. Layout containment then drops it.
    {
        Arena* arena = ArenaNew();
        TrackSizingFunction track = TrackSizingFunction::Auto();
        GridTemplateComponent cols[2] = {GridTemplateComponent::Single(track),
                                         GridTemplateComponent::Single(track)};
        auto gridBaseline = [&](Contain contain, float siblingY) {
            TaffyTree tree;
            tree.Init();
            BaselineContext content = {{30.0f, 30.0f}, Some(10.0f)};
            taffy::Style cell;
            cell.size = SizeDim::FromLengths(30.0f, 30.0f);
            TaffyNodeId a = tree.NewLeafWithContext(cell, &content);
            TaffyNodeId b = tree.NewLeafWithContext(cell, &content);
            taffy::Style gridStyle;
            gridStyle.display = taffy::Display::Grid;
            gridStyle.contain = contain;
            gridStyle.alignItems = AlignItems{AlignItemsKeyword::Baseline};
            gridStyle.gridTemplateColumns = SliceDup(arena, cols, 2);
            TaffyNodeId cells[] = {a, b};
            TaffyNodeId grid = tree.NewWithChildren(gridStyle, cells, 2);
            BaselineContext refCtx = {{40.0f, 80.0f}, Some(10.0f)};
            TaffyNodeId sibling =
                tree.NewLeafWithContext(taffy::Style{}, &refCtx);
            taffy::Style rootStyle;
            rootStyle.alignItems = AlignItems{AlignItemsKeyword::Baseline};
            rootStyle.size = SizeDim::FromLengths(200.0f, 120.0f);
            TaffyNodeId kids[] = {grid, sibling};
            TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
            tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                          BaselineMeasure, nullptr);
            utassertnear(tree.GetLayout(grid).location.y, 0.0f);
            utassertnear(tree.GetLayout(sibling).location.y, siblingY);
            tree.Free();
        };
        gridBaseline(Contain::Paint(), 0.0f);
        gridBaseline(Contain::Layout(), 20.0f);
        ArenaDelete(arena);
    }

    auto overflowOf = [](Contain contain) {
        TaffyTree tree;
        tree.Init();
        taffy::Style absStyle;
        absStyle.position = taffy::Position::Absolute;
        absStyle.inset.left = LengthPercentageAuto::Length(-30.0f);
        absStyle.inset.top = LengthPercentageAuto::Length(-20.0f);
        absStyle.size = SizeDim::FromLengths(40.0f, 20.0f);
        TaffyNodeId abs = tree.NewLeaf(absStyle);
        taffy::Style midStyle;
        midStyle.display = taffy::Display::Block;
        midStyle.position = taffy::Position::Relative;
        midStyle.contain = contain;
        midStyle.size = SizeDim::FromLengths(50.0f, 50.0f);
        TaffyNodeId mid = tree.NewWithChildren(midStyle, &abs, 1);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.size = SizeDim::FromLengths(100.0f, 100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &mid, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        RectF rect = tree.GetLayout(root).scrollableOverflowRect;
        tree.Free();
        return rect;
    };
    RectF open = overflowOf(Contain::None());
    utassertnear(open.left, -30.0f);
    utassertnear(open.top, -20.0f);
    RectF laidOut = overflowOf(Contain::Layout());
    utassertnear(laidOut.left, 0.0f);
    utassertnear(laidOut.top, 0.0f);
    RectF painted = overflowOf(Contain::Paint());
    utassertnear(painted.left, 0.0f);
    utassertnear(painted.top, 0.0f);
}

// Three block children. The middle one's margins collapse through it only
// when its used height is zero, so the two 10px boxes share one 10px gap.
static void CollapseThroughCase(const taffy::Style& mid, float rootH,
                                float midY, float midH, float lastY) {
    TaffyTree tree;
    tree.Init();
    taffy::Style box;
    box.display = taffy::Display::Block;
    box.size.height = Dimension::Length(10.0f);
    taffy::Style first = box;
    first.margin.bottom = LengthPercentageAuto::Length(10.0f);
    taffy::Style last = box;
    last.margin.top = LengthPercentageAuto::Length(10.0f);
    TaffyNodeId a = tree.NewLeaf(first);
    taffy::Style emptyStyle;
    emptyStyle.display = taffy::Display::Block;
    TaffyNodeId empty = tree.NewLeaf(emptyStyle);
    taffy::Style midStyle = mid;
    midStyle.display = taffy::Display::Block;
    TaffyNodeId b = tree.NewWithChildren(midStyle, &empty, 1);
    TaffyNodeId c = tree.NewLeaf(last);
    TaffyNodeId kids[] = {a, b, c};
    taffy::Style rootStyle;
    rootStyle.display = taffy::Display::Block;
    rootStyle.size.width = Dimension::Length(50.0f);
    if (rootH > 0.0f) {
        rootStyle.size.height = Dimension::Length(rootH);
    }
    TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 3);
    tree.ComputeLayout(root, SizeAvail::MaxContent());
    utassertnear(tree.GetLayout(root).size.h, rootH > 0.0f ? rootH : 30.0f);
    AssertBox(tree.GetLayout(a), 0.0f, 0.0f, 50.0f, 10.0f);
    AssertBox(tree.GetLayout(b), 0.0f, midY, 50.0f, midH);
    AssertBox(tree.GetLayout(c), 0.0f, lastY, 50.0f, 10.0f);
    tree.Free();
}

// Children of a parent at a fractional offset tile without 1px gaps.
// rounding_fractial_input_8: padding 20.625 / 51.25, ten 13.9375 boxes.
static void TestRoundingCumulativePosition() {
    TestSuite("taffy rounding cumulative position");

    TaffyTree tree;
    tree.Init();
    taffy::Style item;
    item.size = SizeDim::FromLengths(13.9375f, 13.9375f);
    TaffyNodeId kids[10];
    for (int i = 0; i < 10; i++) {
        kids[i] = tree.NewLeaf(item);
    }
    taffy::Style column;
    column.flexDirection = FlexDirection::Column;
    TaffyNodeId col = tree.NewWithChildren(column, kids, 10);
    taffy::Style rootStyle;
    rootStyle.size = SizeDim::FromLengths(100.0f, 200.0f);
    rootStyle.padding.left = LengthPercentage::Length(20.625f);
    rootStyle.padding.top = LengthPercentage::Length(51.25f);
    TaffyNodeId root = tree.NewWithChildren(rootStyle, &col, 1);
    tree.ComputeLayout(root, SizeAvail::MaxContent());
    AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 100.0f, 200.0f);
    AssertBox(tree.GetLayout(col), 21.0f, 51.0f, 14.0f, 149.0f);
    for (int i = 0; i < 10; i++) {
        AssertBox(tree.GetLayout(kids[i]), 0.0f, 14.0f * (float)i, 14.0f,
                  14.0f);
    }
    tree.Free();
}

static void TestMarginCollapseThroughUsedHeight() {
    TestSuite("taffy margin collapse through used height");

    taffy::Style percent;
    percent.size.height = Dimension::Percent(0.10f);
    percent.margin.top = LengthPercentageAuto::Length(10.0f);
    percent.margin.bottom = LengthPercentageAuto::Length(10.0f);
    CollapseThroughCase(percent, 100.0f, 20.0f, 10.0f, 40.0f);

    taffy::Style clamped;
    clamped.size.height = Dimension::Length(10.0f);
    clamped.maxSize.height = Dimension::Length(0.0f);
    clamped.margin.top = LengthPercentageAuto::Length(10.0f);
    clamped.margin.bottom = LengthPercentageAuto::Length(10.0f);
    CollapseThroughCase(clamped, 0.0f, 20.0f, 0.0f, 20.0f);

    taffy::Style minPercent;
    minPercent.minSize.height = Dimension::Percent(0.10f);
    minPercent.margin.top = LengthPercentageAuto::Length(10.0f);
    minPercent.margin.bottom = LengthPercentageAuto::Length(10.0f);
    CollapseThroughCase(minPercent, 0.0f, 20.0f, 0.0f, 20.0f);
}

// Vertical percentage padding resolves against the container's width when
// the container's height is auto. 10% + 20% of 100px is 30px of padding,
// which floors the border box; the inner 10px block sits under the 15%
// left padding.
static void TestBlockPercentPaddingUsesWidth() {
    TestSuite("taffy block percent padding uses width");

    TaffyTree tree;
    tree.Init();
    taffy::Style inner;
    inner.display = taffy::Display::Block;
    inner.size.height = Dimension::Length(10.0f);
    TaffyNodeId grandchild = tree.NewLeaf(inner);
    taffy::Style mid;
    mid.display = taffy::Display::Block;
    mid.size.height = Dimension::Length(20.0f);
    mid.padding.top = LengthPercentage::Percent(0.10f);
    mid.padding.right = LengthPercentage::Percent(0.05f);
    mid.padding.bottom = LengthPercentage::Percent(0.20f);
    mid.padding.left = LengthPercentage::Percent(0.15f);
    TaffyNodeId child = tree.NewWithChildren(mid, &grandchild, 1);
    taffy::Style rootStyle;
    rootStyle.display = taffy::Display::Block;
    rootStyle.size.width = Dimension::Length(100.0f);
    TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
    tree.ComputeLayout(root, SizeAvail::MaxContent());
    AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 100.0f, 30.0f);
    AssertBox(tree.GetLayout(child), 0.0f, 0.0f, 100.0f, 30.0f);
    AssertBox(tree.GetLayout(grandchild), 15.0f, 10.0f, 80.0f, 10.0f);
    tree.Free();
}

// A column flex item's height is not a percentage basis while its content
// size is measured, and it does not raise the column's max-content height
// above the flex basis. The 200px block and its 100% child both end at 100.
static void TestContentSizeIgnoresOwnHeightBasis() {
    TestSuite("taffy content size ignores own height basis");

    TaffyTree tree;
    tree.Init();
    taffy::Style inner;
    inner.display = taffy::Display::Block;
    inner.size.height = Dimension::Percent(1.0f);
    TaffyNodeId grandchild = tree.NewLeaf(inner);
    taffy::Style block;
    block.display = taffy::Display::Block;
    block.flexBasis = Dimension::Length(100.0f);
    block.size.height = Dimension::Length(200.0f);
    TaffyNodeId child = tree.NewWithChildren(block, &grandchild, 1);
    taffy::Style rootStyle;
    rootStyle.display = taffy::Display::Flex;
    rootStyle.flexDirection = FlexDirection::Column;
    rootStyle.size.width = Dimension::Length(100.0f);
    TaffyNodeId root = tree.NewWithChildren(rootStyle, &child, 1);
    tree.ComputeLayout(root, SizeAvail::MaxContent());
    AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 100.0f, 100.0f);
    AssertBox(tree.GetLayout(child), 0.0f, 0.0f, 100.0f, 100.0f);
    AssertBox(tree.GetLayout(grandchild), 0.0f, 0.0f, 100.0f, 100.0f);
    tree.Free();
}

// An auto-height wrapping column does not wrap against a definite height
// handed down as available space. The two items stay in one column.
static void TestFlexColumnWrapIgnoresAncestorHeight() {
    TestSuite("taffy flex column wrap ancestor height");

    TaffyTree tree;
    tree.Init();
    taffy::Style item;
    item.size = SizeDim::FromLengths(20.0f, 60.0f);
    TaffyNodeId a = tree.NewLeaf(item);
    TaffyNodeId b = tree.NewLeaf(item);
    TaffyNodeId kids[] = {a, b};
    taffy::Style rootStyle;
    rootStyle.flexDirection = FlexDirection::Column;
    rootStyle.flexWrap = FlexWrap::Wrap;
    rootStyle.size.width = Dimension::Length(40.0f);
    TaffyNodeId root = tree.NewWithChildren(rootStyle, kids, 2);
    tree.ComputeLayout(root, SizeAvail::Definite({40.0f, 100.0f}));
    AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 40.0f, 120.0f);
    AssertBox(tree.GetLayout(a), 0.0f, 0.0f, 20.0f, 60.0f);
    AssertBox(tree.GetLayout(b), 0.0f, 60.0f, 20.0f, 60.0f);
    tree.Free();
}

// A column's intrinsic height floors by the flex basis first, then adds a
// negative main-axis margin. Flooring after the margin would cancel it.
static void TestFlexMarginAfterBasisFloor() {
    TestSuite("taffy flex margin after basis floor");

    auto run = [](float flexShrink) {
        TaffyTree tree;
        tree.Init();
        taffy::Style child;
        child.flexGrow = 1.0f;
        child.flexShrink = flexShrink;
        child.size = SizeDim::FromLengths(100.0f, 100.0f);
        child.margin.top = LengthPercentageAuto::Length(-24.0f);
        TaffyNodeId childNode = tree.NewLeaf(child);
        taffy::Style rootStyle;
        rootStyle.flexDirection = FlexDirection::Column;
        rootStyle.size.width = Dimension::Length(300.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &childNode, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 300.0f, 76.0f);
        AssertBox(tree.GetLayout(childNode), 0.0f, -24.0f, 100.0f, 100.0f);
        tree.Free();
    };
    run(0.0f);
    run(1.0f);
}

// A stretched definite cross size transfers through the aspect ratio into
// the flex base size, so an auto main size is that transferred length.
static void TestFlexAspectRatioStretch() {
    TestSuite("taffy flex aspect ratio stretch");

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style child;
        child.aspectRatio = Some(2.0f);
        TaffyNodeId childNode = tree.NewLeaf(child);
        taffy::Style rootStyle;
        rootStyle.size.height = Dimension::Length(100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &childNode, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 200.0f, 100.0f);
        AssertBox(tree.GetLayout(childNode), 0.0f, 0.0f, 200.0f, 100.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style child;
        child.aspectRatio = Some(0.5f);
        TaffyNodeId childNode = tree.NewLeaf(child);
        taffy::Style rootStyle;
        rootStyle.flexDirection = FlexDirection::Column;
        rootStyle.size.width = Dimension::Length(100.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &childNode, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 100.0f, 200.0f);
        AssertBox(tree.GetLayout(childNode), 0.0f, 0.0f, 100.0f, 200.0f);
        tree.Free();
    }
}

// src/tree/cache.rs. A measure input whose known width is the cache key.
static LayoutInput CacheMeasureInput(float width) {
    LayoutInput in;
    in.runMode = RunMode::ComputeSize;
    in.sizingMode = SizingMode::InherentSize;
    in.axis = RequestedAxis::Both;
    in.knownDimensions.w = Some(width);
    in.knownDimensions.h = None();
    in.knownDimensionsAreDefiniteW = true;
    in.knownDimensionsAreDefiniteH = true;
    in.parentSize = SizeFOptNone();
    in.availableSpace = SizeAvail::MaxContent();
    in.verticalMarginsAreCollapsible = LineBool::False();
    return in;
}

static LayoutOutput CacheMeasureOutput(float width) {
    return LayoutOutput::FromOuterSize({width, width});
}

static bool CacheGotSize(Cache& cache, float width, SizeF* size) {
    LayoutOutput out;
    if (!cache.Get(CacheMeasureInput(width), &out)) {
        return false;
    }
    *size = out.size;
    return true;
}

static int CacheMeasureCount(const Cache& cache) {
    int n = 0;
    for (int i = 0; i < kCacheSize; i++) {
        if ((cache.presentMask & Cache::MeasureBit(i)) != 0) {
            n++;
        }
    }
    return n;
}

static void TestLayoutCache() {
    TestSuite("taffy::tree::cache");

    {
        // recently_used_measure_entries_get_a_second_chance
        Cache cache;
        for (int width = 0; width < kCacheSize; width++) {
            cache.Store(CacheMeasureInput((float)width),
                        CacheMeasureOutput((float)width));
        }
        cache.Store(CacheMeasureInput((float)kCacheSize),
                    CacheMeasureOutput((float)kCacheSize));
        SizeF got;
        utassert(CacheGotSize(cache, 1.0f, &got));
        utassertnear(got.w, 1.0f);
        utassertnear(got.h, 1.0f);
        cache.Store(CacheMeasureInput((float)(kCacheSize + 1)),
                    CacheMeasureOutput((float)(kCacheSize + 1)));
        utassert(CacheGotSize(cache, 1.0f, &got));
        utassertnear(got.w, 1.0f);
        utassertnear(got.h, 1.0f);
        utassert(!CacheGotSize(cache, 2.0f, &got));
    }

    {
        // storing_an_existing_measurement_updates_it_in_place
        Cache cache;
        cache.Store(CacheMeasureInput(1.0f), CacheMeasureOutput(1.0f));
        cache.Store(CacheMeasureInput(2.0f), CacheMeasureOutput(2.0f));
        cache.Store(CacheMeasureInput(1.0f), CacheMeasureOutput(3.0f));
        utassert(CacheMeasureCount(cache) == 2);
        SizeF got;
        utassert(CacheGotSize(cache, 1.0f, &got));
        utassertnear(got.w, 3.0f);
        utassertnear(got.h, 3.0f);
    }

    {
        // measurements_with_margin_collapse_metadata_are_not_cached
        Cache cache;
        LayoutOutput collapseThrough = CacheMeasureOutput(1.0f);
        collapseThrough.marginsCanCollapseThrough = true;
        cache.Store(CacheMeasureInput(1.0f), collapseThrough);
        SizeF got;
        utassert(!CacheGotSize(cache, 1.0f, &got));

        LayoutOutput carried = CacheMeasureOutput(2.0f);
        carried.topMargin = CollapsibleMarginSet::FromMargin(10.0f);
        cache.Store(CacheMeasureInput(2.0f), carried);
        utassert(!CacheGotSize(cache, 2.0f, &got));
    }

    {
        // retrieving_a_measurement_only_marks_its_slot_as_used
        Cache cache;
        cache.Store(CacheMeasureInput(1.0f), CacheMeasureOutput(1.0f));
        cache.Store(CacheMeasureInput(2.0f), CacheMeasureOutput(2.0f));
        cache.recentlyUsedEntries = 0;
        CacheEntry<SizeF> before[kCacheSize];
        for (int i = 0; i < kCacheSize; i++) {
            before[i] = cache.measureEntries[i];
        }
        SizeF got;
        utassert(CacheGotSize(cache, 1.0f, &got));
        utassertnear(got.w, 1.0f);
        utassertnear(got.h, 1.0f);
        for (int i = 0; i < kCacheSize; i++) {
            utassert(before[i].key == cache.measureEntries[i].key);
            utassert(before[i].content == cache.measureEntries[i].content);
        }
        utassert(cache.recentlyUsedEntries != 0);
    }

    {
        // A different sizing mode or margin collapsibility misses, and
        // storing one drops the entries computed with the previous mode.
        Cache cache;
        cache.Store(CacheMeasureInput(1.0f), CacheMeasureOutput(1.0f));
        LayoutInput other = CacheMeasureInput(1.0f);
        other.sizingMode = SizingMode::ContentSize;
        LayoutOutput missed;
        utassert(!cache.Get(other, &missed));
        other = CacheMeasureInput(1.0f);
        other.verticalMarginsAreCollapsible.start = true;
        utassert(!cache.Get(other, &missed));
        SizeF got;
        utassert(CacheGotSize(cache, 1.0f, &got));
        utassertnear(got.w, 1.0f);

        other = CacheMeasureInput(3.0f);
        other.sizingMode = SizingMode::ContentSize;
        cache.Store(other, CacheMeasureOutput(3.0f));
        utassert(!CacheGotSize(cache, 1.0f, &got));
        utassert(cache.Get(other, &missed));
        utassertnear(missed.size.w, 3.0f);
        utassert(CacheMeasureCount(cache) == 1);

        cache.Store(LayoutInput::Hidden(), CacheMeasureOutput(9.0f));
        utassert(cache.Get(other, &missed));
        utassertnear(missed.size.w, 3.0f);
    }
}

struct MeasureCount {
    int count = 0;
};

static SizeF CountedLeafInner(SizeFOpt known, SizeAvail, void* ctx) {
    MeasureCount* c = (MeasureCount*)ctx;
    c->count++;
    float w = IsSome(known.w) ? known.w : 10.0f;
    float h = IsSome(known.h) ? known.h : 10.0f;
    return {w, h};
}

static taffy::LayoutOutput CountedLeafMeasure(const taffy::LayoutInput& inputs,
                                              TaffyNodeId node,
                                              void* nodeContext,
                                              const taffy::Style* style,
                                              void* userData) {
    (void)node;
    (void)userData;
    return taffy::ComputeLeafLayout(inputs, *style, taffy::CalcResolver{},
                                    CountedLeafInner, nodeContext);
}

// An item that spans only a fixed track and a flexible track is not measured
// during intrinsic track sizing. The one measure is the final layout pass.
static void TestGridUnaffectedTracksNotMeasured() {
    TestSuite("taffy grid unaffected tracks not measured");

    TaffyTree tree;
    tree.Init();
    MeasureCount ctx;
    taffy::Style leafStyle;
    leafStyle.gridColumn = {GLine(2), GLine(3)};
    TaffyNodeId leaf = tree.NewLeafWithContext(leafStyle, &ctx);

    TrackSizingFunction cols[2] = {
        TrackSizingFunction::Length(100.0f),
        TrackSizingFunction::MinMax(MinTrackSizingFunction::Length(0.0f),
                                    MaxTrackSizingFunction::Fr(1.0f))};
    TrackSizingFunction row = TrackSizingFunction::Length(50.0f);
    GridTemplateComponent colC[2] = {GridTemplateComponent::Single(cols[0]),
                                     GridTemplateComponent::Single(cols[1])};
    GridTemplateComponent rowC = GridTemplateComponent::Single(row);
    taffy::Style gridStyle;
    gridStyle.display = taffy::Display::Grid;
    gridStyle.size = SizeDim::FromLengths(400.0f, 50.0f);
    gridStyle.gridTemplateColumns = GridTemplateOf(colC, 2);
    gridStyle.gridTemplateRows = GridTemplateOf(&rowC, 1);
    TaffyNodeId grid = tree.NewWithChildren(gridStyle, &leaf, 1);
    tree.ComputeLayoutWithMeasure(grid, SizeAvail::MaxContent(),
                                  CountedLeafMeasure, nullptr);
    utassertnear(tree.GetLayout(leaf).size.w, 300.0f);
    utassert(ctx.count == 1);
    tree.Free();
}

// "HHHHH" + ZWSP + "HHHH" at 10px: min-content 50, max-content 90, and two
// lines once a definite width is below that max-content width.
static SizeF WrapAtColumnInner(SizeFOpt known, SizeAvail available, void*) {
    float width;
    if (IsSome(known.w)) {
        width = known.w;
    } else if (available.width.kind == AvailableSpace::Kind::Definite) {
        width = available.width.value;
    } else if (available.width.kind == AvailableSpace::Kind::MinContent) {
        width = 50.0f;
    } else {
        width = 90.0f;
    }
    float height;
    if (IsSome(known.h)) {
        height = known.h;
    } else if (width + 0.001f < 90.0f) {
        height = 20.0f;
    } else {
        height = 10.0f;
    }
    return {width, height};
}

static taffy::LayoutOutput WrapAtColumnMeasure(const taffy::LayoutInput& inputs,
                                               TaffyNodeId node,
                                               void* nodeContext,
                                               const taffy::Style* style,
                                               void* userData) {
    (void)node;
    (void)nodeContext;
    (void)userData;
    return taffy::ComputeLeafLayout(inputs, *style, taffy::CalcResolver{},
                                    WrapAtColumnInner, nullptr);
}

// A 1fr row's max-content contribution wraps against the column width.
// Measuring it with no known width would leave the row at a single line.
static void TestGridFrRowUsesColumnWidth() {
    TestSuite("taffy grid fr row uses column width");

    TaffyTree tree;
    tree.Init();
    TaffyNodeId a = tree.NewLeaf(taffy::Style{});
    TaffyNodeId b = tree.NewLeaf(taffy::Style{});
    TaffyNodeId kids[] = {a, b};
    TrackSizingFunction col = TrackSizingFunction::Length(50.0f);
    TrackSizingFunction rows[2] = {
        TrackSizingFunction::MinMax(MinTrackSizingFunction::Length(10.0f),
                                    MaxTrackSizingFunction::Fr(1.0f)),
        TrackSizingFunction::MinMax(MinTrackSizingFunction::MinContent(),
                                    MaxTrackSizingFunction::Length(50.0f))};
    GridTemplateComponent colC = GridTemplateComponent::Single(col);
    GridTemplateComponent rowC[2] = {GridTemplateComponent::Single(rows[0]),
                                     GridTemplateComponent::Single(rows[1])};
    taffy::Style gridStyle;
    gridStyle.display = taffy::Display::Grid;
    gridStyle.gridTemplateColumns = GridTemplateOf(&colC, 1);
    gridStyle.gridTemplateRows = GridTemplateOf(rowC, 2);
    TaffyNodeId grid = tree.NewWithChildren(gridStyle, kids, 2);
    tree.ComputeLayoutWithMeasure(grid, SizeAvail::MaxContent(),
                                  WrapAtColumnMeasure, nullptr);
    utassertnear(tree.GetLayout(grid).size.w, 50.0f);
    utassertnear(tree.GetLayout(grid).size.h, 70.0f);
    utassertnear(tree.GetLayout(a).size.w, 50.0f);
    utassertnear(tree.GetLayout(a).size.h, 20.0f);
    utassertnear(tree.GetLayout(b).location.y, 20.0f);
    utassertnear(tree.GetLayout(b).size.h, 50.0f);
    tree.Free();
}

// A width-only block measure stores height 0. A later height query must not
// reuse that slot, or intrinsically sized grid rows collapse.
static void TestGridBlockItemHeightCache() {
    TestSuite("taffy grid block item height cache");

    TaffyTree tree;
    tree.Init();
    SizeF leafSize = {100.0f, 19.0f};
    TaffyNodeId items[3];
    for (int row = 1; row <= 3; row++) {
        TaffyNodeId leaf = tree.NewLeafWithContext(taffy::Style{}, &leafSize);
        taffy::Style item;
        item.display = taffy::Display::Block;
        item.gridRow = {GLine((int16_t)row), GLine((int16_t)(row + 1))};
        item.gridColumn = {GLine(1), GLine(2)};
        items[row - 1] = tree.NewWithChildren(item, &leaf, 1);
    }

    TrackSizingFunction cols[2] = {TrackSizingFunction::Percent(0.6f),
                                   TrackSizingFunction::Auto()};
    GridTemplateComponent colComps[2] = {
        GridTemplateComponent::Single(cols[0]),
        GridTemplateComponent::Single(cols[1])};
    taffy::Style rootStyle;
    rootStyle.display = taffy::Display::Grid;
    rootStyle.gridTemplateColumns = GridTemplateOf(colComps, 2);
    rootStyle.size.width = Dimension::Length(1000.0f);
    rootStyle.size.height = Dimension::Auto();
    TaffyNodeId root = tree.NewWithChildren(rootStyle, items, 3);
    tree.ComputeLayoutWithMeasure(root, SizeAvail::Definite({1000.0f, 1000.0f}),
                                  SizeMeasureFunction, nullptr);
    for (int i = 0; i < 3; i++) {
        utassertnear(tree.GetLayout(items[i]).size.h, 19.0f);
        utassertnear(tree.GetLayout(items[i]).location.y, 19.0f * (float)i);
    }
    tree.Free();
}

// A leaf whose horizontal margins exceed a 20px container. Records whether
// any measure saw a negative known size or a negative definite available space.
// The measure itself is zero unless a known size is already set, matching
// taffy's TestNodeContext::zero.
struct NegativeSpaceCtx {
    bool saw = false;
};

static bool LayoutInputIsNegative(const taffy::LayoutInput& inputs) {
    auto neg = [](Optf v) { return IsSome(v) && v < 0.0f; };
    auto negAvail = [](AvailableSpace a) {
        return a.kind == AvailableSpace::Kind::Definite && a.value < 0.0f;
    };
    return neg(inputs.knownDimensions.w) || neg(inputs.knownDimensions.h) ||
           negAvail(inputs.availableSpace.width) ||
           negAvail(inputs.availableSpace.height);
}

static SizeF ZeroUnlessKnown(SizeFOpt known, SizeAvail, void*) {
    return {IsSome(known.w) ? known.w : 0.0f, IsSome(known.h) ? known.h : 0.0f};
}

static taffy::LayoutOutput NegativeSpaceMeasure(
    const taffy::LayoutInput& inputs, TaffyNodeId node, void* nodeContext,
    const taffy::Style* style, void* userData) {
    (void)node;
    (void)userData;
    NegativeSpaceCtx* ctx = (NegativeSpaceCtx*)nodeContext;
    if (ctx && LayoutInputIsNegative(inputs)) {
        ctx->saw = true;
    }
    return taffy::ComputeLeafLayout(inputs, *style, taffy::CalcResolver{},
                                    ZeroUnlessKnown, nullptr);
}

static void AssertMarginsFloorAtZero(taffy::Display display,
                                     taffy::FlexDirection dir) {
    TaffyTree tree;
    tree.Init();
    NegativeSpaceCtx ctx;
    taffy::Style leafStyle;
    leafStyle.margin.left = LengthPercentageAuto::Length(30.0f);
    leafStyle.margin.right = LengthPercentageAuto::Length(10.0f);
    leafStyle.size = {Dimension::Auto(), Dimension::Auto()};
    TaffyNodeId leaf = tree.NewLeafWithContext(leafStyle, &ctx);

    TrackSizingFunction col = TrackSizingFunction::Length(20.0f);
    GridTemplateComponent colC = GridTemplateComponent::Single(col);
    taffy::Style rootStyle;
    rootStyle.display = display;
    rootStyle.flexDirection = dir;
    rootStyle.size.width = Dimension::Length(20.0f);
    rootStyle.size.height = Dimension::Auto();
    rootStyle.gridTemplateColumns = GridTemplateOf(&colC, 1);
    TaffyNodeId root = tree.NewWithChildren(rootStyle, &leaf, 1);
    tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                  NegativeSpaceMeasure, nullptr);
    utassert(!ctx.saw);
    utassertnear(tree.GetLayout(leaf).size.w, 0.0f);
    tree.Free();
}

// Block, column flex and grid each floor a child's available and stretch
// width when its horizontal margins are wider than the container.
static void TestNegativeAvailableSpace() {
    TestSuite("taffy negative available space");
    AssertMarginsFloorAtZero(taffy::Display::Block, FlexDirection::Row);
    AssertMarginsFloorAtZero(taffy::Display::Flex, FlexDirection::Column);
    AssertMarginsFloorAtZero(taffy::Display::Grid, FlexDirection::Row);
}

// oof.rs resolve_static_offset. Area is start 10, end 110.
static void TestResolveStaticOffset() {
    TestSuite("taffy resolve static offset");
    AxisStaticPosition start;
    start.start = 10.0f;
    start.end = 110.0f;
    start.align = AxisStaticAlign::FromKeyword(AxisStaticEdge::Start);
    utassertnear(ResolveStaticOffsetAxis(start, 20.0f, 0.0f, 0.0f), 10.0f);
    utassertnear(ResolveStaticOffsetAxis(start, 20.0f, 5.0f, 15.0f), 15.0f);

    AxisStaticPosition end = start;
    end.align = AxisStaticAlign::FromKeyword(AxisStaticEdge::End);
    utassertnear(ResolveStaticOffsetAxis(end, 20.0f, 0.0f, 0.0f), 90.0f);
    utassertnear(ResolveStaticOffsetAxis(end, 20.0f, 5.0f, 15.0f), 75.0f);

    AxisStaticPosition center = start;
    center.align = AxisStaticAlign::FromKeyword(AxisStaticEdge::Center);
    utassertnear(ResolveStaticOffsetAxis(center, 20.0f, 0.0f, 0.0f), 50.0f);
    utassertnear(ResolveStaticOffsetAxis(center, 20.0f, 5.0f, 15.0f), 45.0f);

    AxisStaticPosition safe = start;
    safe.align.keyword = AxisStaticEdge::End;
    safe.align.safety = AlignmentSafety::Safe;
    safe.align.fallback = AxisStaticEdge::Start;
    utassertnear(ResolveStaticOffsetAxis(safe, 20.0f, 0.0f, 0.0f), 90.0f);
    utassertnear(ResolveStaticOffsetAxis(safe, 100.0f, 0.0f, 0.0f), 10.0f);
    utassertnear(ResolveStaticOffsetAxis(safe, 120.0f, 0.0f, 0.0f), 10.0f);
    utassertnear(ResolveStaticOffsetAxis(safe, 90.0f, 5.0f, 15.0f), 15.0f);

    AxisStaticPosition unsafeEnd = safe;
    unsafeEnd.align.safety = AlignmentSafety::Unsafe;
    utassertnear(ResolveStaticOffsetAxis(unsafeEnd, 120.0f, 0.0f, 0.0f),
                 -10.0f);

    AxisStaticPosition anchor =
        AxisStaticPosition::FromEdge(40.0f, AxisStaticEdge::Start);
    utassertnear(ResolveStaticOffsetAxis(anchor, 20.0f, 0.0f, 0.0f), 40.0f);
    AxisStaticPosition anchorEnd =
        AxisStaticPosition::FromEdge(40.0f, AxisStaticEdge::End);
    utassertnear(ResolveStaticOffsetAxis(anchorEnd, 20.0f, 0.0f, 0.0f), 20.0f);
}

static taffy::Style OofLeaf(float w, float h) {
    taffy::Style s;
    s.size = SizeDim::FromLengths(w, h);
    return s;
}

static void AssertLoc(const TaffyTree& tree, TaffyNodeId node, float x,
                      float y) {
    utassertnear(tree.GetLayout(node).location.x, x);
    utassertnear(tree.GetLayout(node).location.y, y);
}

// tests/hand_written/oof_hoisting.rs. Absolute and fixed boxes are laid out
// by their containing block, and the location is relative to that block.
static void TestOofHoisting() {
    TestSuite("taffy out-of-flow hoisting");
    taffy::Display displays[] = {taffy::Display::Block, taffy::Display::Flex,
                                 taffy::Display::Grid};

    for (taffy::Display d : displays) {
        TaffyTree tree;
        tree.Init();
        taffy::Style absStyle;
        absStyle.position = taffy::Position::Absolute;
        absStyle.inset.left = LengthPercentageAuto::Length(10.0f);
        absStyle.inset.top = LengthPercentageAuto::Length(20.0f);
        absStyle.size = SizeDim::FromLengths(30.0f, 30.0f);
        TaffyNodeId abs = tree.NewLeaf(absStyle);
        taffy::Style parent = OofLeaf(50.0f, 50.0f);
        parent.display = d;
        parent.margin.left = LengthPercentageAuto::Length(40.0f);
        parent.margin.top = LengthPercentageAuto::Length(40.0f);
        TaffyNodeId staticParent = tree.NewWithChildren(parent, &abs, 1);
        taffy::Style cbStyle = OofLeaf(200.0f, 200.0f);
        cbStyle.display = d;
        cbStyle.position = taffy::Position::Relative;
        TaffyNodeId cb = tree.NewWithChildren(cbStyle, &staticParent, 1);
        tree.ComputeLayout(cb, SizeAvail::MaxContent());
        AssertLoc(tree, abs, 10.0f, 20.0f);
        utassertnear(tree.GetLayout(abs).size.w, 30.0f);
        utassertnear(tree.GetLayout(abs).size.h, 30.0f);
        tree.Free();
    }

    for (taffy::Display d : displays) {
        TaffyTree tree;
        tree.Init();
        taffy::Style absStyle;
        absStyle.position = taffy::Position::Absolute;
        absStyle.size = SizeDim::FromLengths(10.0f, 10.0f);
        taffy::Style inflow = OofLeaf(20.0f, 20.0f);
        inflow.display = d;
        TaffyNodeId directA = tree.NewLeaf(absStyle);
        TaffyNodeId nestedB = tree.NewLeaf(absStyle);
        TaffyNodeId staticB = tree.NewWithChildren(inflow, &nestedB, 1);
        TaffyNodeId nestedC = tree.NewLeaf(absStyle);
        TaffyNodeId staticC = tree.NewWithChildren(inflow, &nestedC, 1);
        TaffyNodeId directD = tree.NewLeaf(absStyle);
        TaffyNodeId nestedE = tree.NewLeaf(absStyle);
        TaffyNodeId staticE = tree.NewWithChildren(inflow, &nestedE, 1);
        TaffyNodeId kids[] = {directA, staticB, staticC, directD, staticE};
        taffy::Style cbStyle = OofLeaf(50.0f, 200.0f);
        cbStyle.display = d;
        cbStyle.position = taffy::Position::Relative;
        cbStyle.flexWrap = FlexWrap::WrapReverse;
        cbStyle.flexDirection = FlexDirection::RowReverse;
        TaffyNodeId cb = tree.NewWithChildren(cbStyle, kids, 5);
        tree.ComputeLayout(cb, SizeAvail::MaxContent());
        utassert(tree.HoistedChildCount(cb) == 5);
        utassert(tree.HoistedChildAt(cb, 0) == directA);
        utassert(tree.HoistedChildAt(cb, 1) == nestedB);
        utassert(tree.HoistedChildAt(cb, 2) == nestedC);
        utassert(tree.HoistedChildAt(cb, 3) == directD);
        utassert(tree.HoistedChildAt(cb, 4) == nestedE);
        tree.Free();
    }

    for (taffy::Display d : displays) {
        TaffyTree tree;
        tree.Init();
        taffy::Style absStyle;
        absStyle.position = taffy::Position::Absolute;
        absStyle.size = SizeDim::FromLengths(30.0f, 30.0f);
        TaffyNodeId abs = tree.NewLeaf(absStyle);
        TaffyNodeId sibling = tree.NewLeaf(OofLeaf(50.0f, 50.0f));
        TaffyNodeId pair[] = {sibling, abs};
        taffy::Style parent = OofLeaf(100.0f, 100.0f);
        parent.display = d;
        parent.margin.left = LengthPercentageAuto::Length(40.0f);
        parent.margin.top = LengthPercentageAuto::Length(40.0f);
        TaffyNodeId staticParent = tree.NewWithChildren(parent, pair, 2);
        taffy::Style cbStyle = OofLeaf(200.0f, 200.0f);
        cbStyle.display = d;
        cbStyle.position = taffy::Position::Relative;
        TaffyNodeId cb = tree.NewWithChildren(cbStyle, &staticParent, 1);
        tree.ComputeLayout(cb, SizeAvail::MaxContent());
        if (d == taffy::Display::Block) {
            AssertLoc(tree, abs, 40.0f, 90.0f);
        } else {
            AssertLoc(tree, abs, 40.0f, 40.0f);
        }
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style fixedStyle;
        fixedStyle.position = taffy::Position::Fixed;
        fixedStyle.inset.left = LengthPercentageAuto::Length(5.0f);
        fixedStyle.inset.top = LengthPercentageAuto::Length(5.0f);
        fixedStyle.size = SizeDim::FromLengths(10.0f, 10.0f);
        TaffyNodeId fixed = tree.NewLeaf(fixedStyle);
        taffy::Style parent = OofLeaf(50.0f, 50.0f);
        parent.position = taffy::Position::Relative;
        parent.margin.left = LengthPercentageAuto::Length(50.0f);
        parent.margin.top = LengthPercentageAuto::Length(50.0f);
        TaffyNodeId positioned = tree.NewWithChildren(parent, &fixed, 1);
        TaffyNodeId root =
            tree.NewWithChildren(OofLeaf(200.0f, 200.0f), &positioned, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertLoc(tree, fixed, 5.0f, 5.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style innerStyle;
        innerStyle.position = taffy::Position::Absolute;
        innerStyle.inset.right = LengthPercentageAuto::Length(0.0f);
        innerStyle.inset.bottom = LengthPercentageAuto::Length(0.0f);
        innerStyle.size = SizeDim::FromLengths(10.0f, 10.0f);
        TaffyNodeId inner = tree.NewLeaf(innerStyle);
        taffy::Style outerStyle;
        outerStyle.position = taffy::Position::Absolute;
        outerStyle.inset.left = LengthPercentageAuto::Length(20.0f);
        outerStyle.inset.top = LengthPercentageAuto::Length(20.0f);
        outerStyle.size = SizeDim::FromLengths(50.0f, 50.0f);
        TaffyNodeId outer = tree.NewWithChildren(outerStyle, &inner, 1);
        TaffyNodeId root =
            tree.NewWithChildren(OofLeaf(200.0f, 200.0f), &outer, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertLoc(tree, outer, 20.0f, 20.0f);
        AssertLoc(tree, inner, 40.0f, 40.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style fixedStyle;
        fixedStyle.position = taffy::Position::Fixed;
        fixedStyle.inset.left = LengthPercentageAuto::Length(1.0f);
        fixedStyle.inset.top = LengthPercentageAuto::Length(2.0f);
        fixedStyle.size = SizeDim::FromLengths(10.0f, 10.0f);
        TaffyNodeId fixed = tree.NewLeaf(fixedStyle);
        taffy::Style absStyle;
        absStyle.position = taffy::Position::Absolute;
        absStyle.inset.left = LengthPercentageAuto::Length(20.0f);
        absStyle.inset.top = LengthPercentageAuto::Length(20.0f);
        absStyle.size = SizeDim::FromLengths(50.0f, 50.0f);
        TaffyNodeId abs = tree.NewWithChildren(absStyle, &fixed, 1);
        taffy::Style cbStyle = OofLeaf(100.0f, 100.0f);
        cbStyle.position = taffy::Position::Relative;
        cbStyle.margin.left = LengthPercentageAuto::Length(30.0f);
        cbStyle.margin.top = LengthPercentageAuto::Length(30.0f);
        TaffyNodeId cb = tree.NewWithChildren(cbStyle, &abs, 1);
        TaffyNodeId root =
            tree.NewWithChildren(OofLeaf(200.0f, 200.0f), &cb, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertLoc(tree, abs, 20.0f, 20.0f);
        AssertLoc(tree, fixed, 1.0f, 2.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style absStyle;
        absStyle.position = taffy::Position::Absolute;
        absStyle.inset.left = LengthPercentageAuto::Length(10.0f);
        absStyle.inset.top = LengthPercentageAuto::Length(20.0f);
        absStyle.size = SizeDim::FromLengths(30.0f, 30.0f);
        TaffyNodeId abs = tree.NewLeaf(absStyle);
        TaffyNodeId ancestor =
            tree.NewWithChildren(OofLeaf(50.0f, 50.0f), &abs, 1);
        for (int i = 0; i < 3; i++) {
            ancestor =
                tree.NewWithChildren(OofLeaf(50.0f, 50.0f), &ancestor, 1);
        }
        taffy::Style cbStyle = OofLeaf(200.0f, 200.0f);
        cbStyle.position = taffy::Position::Relative;
        TaffyNodeId cb = tree.NewWithChildren(cbStyle, &ancestor, 1);
        tree.ComputeLayout(cb, SizeAvail::MaxContent());
        AssertLoc(tree, abs, 10.0f, 20.0f);
        tree.ComputeLayout(cb, SizeAvail::MaxContent());
        AssertLoc(tree, abs, 10.0f, 20.0f);
        tree.MarkDirty(cb);
        tree.ComputeLayout(cb, SizeAvail::MaxContent());
        AssertLoc(tree, abs, 10.0f, 20.0f);
        taffy::Style moved = tree.GetStyle(abs);
        moved.inset.left = LengthPercentageAuto::Length(15.0f);
        tree.SetStyle(abs, moved);
        tree.ComputeLayout(cb, SizeAvail::MaxContent());
        AssertLoc(tree, abs, 15.0f, 20.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style fixedStyle = OofLeaf(30.0f, 30.0f);
        fixedStyle.position = taffy::Position::Fixed;
        TaffyNodeId fixed = tree.NewLeaf(fixedStyle);
        TaffyNodeId inner =
            tree.NewWithChildren(OofLeaf(50.0f, 50.0f), &fixed, 1);
        TaffyNodeId root =
            tree.NewWithChildren(OofLeaf(200.0f, 200.0f), &inner, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassert(tree.HoistedChildCount(root) == 1);
        utassert(tree.HoistedChildAt(root, 0) == fixed);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassert(tree.HoistedChildCount(root) == 1);
        utassert(tree.HoistedChildAt(root, 0) == fixed);
        tree.MarkDirty(root);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassert(tree.HoistedChildCount(root) == 1);
        utassert(tree.HoistedChildAt(root, 0) == fixed);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        tree.EnableRounding();
        taffy::Style absStyle;
        absStyle.position = taffy::Position::Absolute;
        absStyle.inset.left = LengthPercentageAuto::Length(10.6f);
        absStyle.inset.top = LengthPercentageAuto::Length(10.6f);
        absStyle.size = SizeDim::FromLengths(10.0f, 10.0f);
        TaffyNodeId abs = tree.NewLeaf(absStyle);
        TaffyNodeId staticParent =
            tree.NewWithChildren(OofLeaf(50.0f, 50.0f), &abs, 1);
        taffy::Style cbStyle = OofLeaf(100.0f, 100.0f);
        cbStyle.position = taffy::Position::Relative;
        cbStyle.margin.left = LengthPercentageAuto::Length(20.3f);
        cbStyle.margin.top = LengthPercentageAuto::Length(20.3f);
        TaffyNodeId cb = tree.NewWithChildren(cbStyle, &staticParent, 1);
        TaffyNodeId root =
            tree.NewWithChildren(OofLeaf(200.0f, 200.0f), &cb, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertLoc(tree, cb, 20.0f, 20.0f);
        AssertLoc(tree, abs, 11.0f, 11.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style absStyle;
        absStyle.position = taffy::Position::Absolute;
        absStyle.inset.left = LengthPercentageAuto::Length(10.0f);
        absStyle.inset.top = LengthPercentageAuto::Length(20.0f);
        absStyle.size = SizeDim::FromLengths(30.0f, 30.0f);
        TaffyNodeId abs = tree.NewLeaf(absStyle);
        taffy::Style middleStyle = OofLeaf(50.0f, 50.0f);
        middleStyle.margin.left = LengthPercentageAuto::Length(40.0f);
        middleStyle.margin.top = LengthPercentageAuto::Length(40.0f);
        TaffyNodeId middle = tree.NewWithChildren(middleStyle, &abs, 1);
        taffy::Style cbStyle = OofLeaf(200.0f, 200.0f);
        cbStyle.position = taffy::Position::Relative;
        TaffyNodeId cb = tree.NewWithChildren(cbStyle, &middle, 1);
        tree.ComputeLayout(cb, SizeAvail::MaxContent());
        AssertLoc(tree, abs, 10.0f, 20.0f);
        taffy::Style positioned = middleStyle;
        positioned.position = taffy::Position::Relative;
        tree.SetStyle(middle, positioned);
        tree.ComputeLayout(cb, SizeAvail::MaxContent());
        AssertLoc(tree, abs, 10.0f, 20.0f);
        AssertLoc(tree, middle, 40.0f, 40.0f);
        tree.SetStyle(middle, middleStyle);
        tree.ComputeLayout(cb, SizeAvail::MaxContent());
        AssertLoc(tree, abs, 10.0f, 20.0f);
        AssertLoc(tree, middle, 40.0f, 40.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style fixedStyle = OofLeaf(10.0f, 10.0f);
        fixedStyle.position = taffy::Position::Fixed;
        fixedStyle.inset.left = LengthPercentageAuto::Length(5.0f);
        fixedStyle.inset.top = LengthPercentageAuto::Length(5.0f);
        TaffyNodeId fixed = tree.NewLeaf(fixedStyle);
        taffy::Style absStyle = OofLeaf(80.0f, 80.0f);
        absStyle.position = taffy::Position::Absolute;
        absStyle.inset.left = LengthPercentageAuto::Length(50.0f);
        absStyle.inset.top = LengthPercentageAuto::Length(50.0f);
        TaffyNodeId abs = tree.NewWithChildren(absStyle, &fixed, 1);
        TaffyNodeId root =
            tree.NewWithChildren(OofLeaf(200.0f, 200.0f), &abs, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertLoc(tree, fixed, 5.0f, 5.0f);
        AssertLoc(tree, abs, 50.0f, 50.0f);
        taffy::Style moved = tree.GetStyle(fixed);
        moved.inset.left = LengthPercentageAuto::Length(7.0f);
        tree.SetStyle(fixed, moved);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertLoc(tree, fixed, 7.0f, 5.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        TaffyNodeId inner = tree.NewLeaf(OofLeaf(10.0f, 10.0f));
        taffy::Style absStyle;
        absStyle.position = taffy::Position::Absolute;
        absStyle.inset.left = LengthPercentageAuto::Length(10.0f);
        absStyle.inset.top = LengthPercentageAuto::Length(10.0f);
        TaffyNodeId abs = tree.NewWithChildren(absStyle, &inner, 1);
        TaffyNodeId staticParent =
            tree.NewWithChildren(OofLeaf(50.0f, 50.0f), &abs, 1);
        taffy::Style cbStyle = OofLeaf(200.0f, 200.0f);
        cbStyle.position = taffy::Position::Relative;
        TaffyNodeId cb = tree.NewWithChildren(cbStyle, &staticParent, 1);
        tree.ComputeLayout(cb, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(abs).size.w, 10.0f);
        utassertnear(tree.GetLayout(abs).size.h, 10.0f);
        tree.SetStyle(inner, OofLeaf(20.0f, 30.0f));
        tree.ComputeLayout(cb, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(abs).size.w, 20.0f);
        utassertnear(tree.GetLayout(abs).size.h, 30.0f);
        tree.Free();
    }
}

// taffy 3307c7e0. An auto margin on an absolute box absorbs free space
// only when both insets on that axis are set. One inset leaves it at 0.
static void TestAbsposAutoMarginNeedsBothInsets() {
    TestSuite("taffy abspos auto margins");
    taffy::Display displays[] = {taffy::Display::Block, taffy::Display::Flex,
                                 taffy::Display::Grid};
    for (taffy::Display d : displays) {
        TaffyTree tree;
        tree.Init();
        taffy::Style absStyle = OofLeaf(40.0f, 20.0f);
        absStyle.position = taffy::Position::Absolute;
        absStyle.inset.right = taffy::LengthPercentageAuto::Length(10.0f);
        absStyle.inset.top = taffy::LengthPercentageAuto::Length(0.0f);
        absStyle.margin.left = taffy::LengthPercentageAuto::Auto();
        absStyle.margin.right = taffy::LengthPercentageAuto::Auto();
        TaffyNodeId abs = tree.NewLeaf(absStyle);
        taffy::Style rootStyle = OofLeaf(200.0f, 200.0f);
        rootStyle.display = d;
        rootStyle.position = taffy::Position::Relative;
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &abs, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertLoc(tree, abs, 150.0f, 0.0f);

        absStyle.inset.left = taffy::LengthPercentageAuto::Length(10.0f);
        tree.SetStyle(abs, absStyle);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertLoc(tree, abs, 80.0f, 0.0f);

        absStyle.inset.right = taffy::LengthPercentageAuto::Auto();
        tree.SetStyle(abs, absStyle);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        AssertLoc(tree, abs, 10.0f, 0.0f);
        tree.Free();
    }
}

// A measure whose unknown axis is `width * heightRatio` (height = width *
// ratio).
struct AspectMeasureCtx {
    float width = 0;
    float heightRatio = 0;
};

static SizeF AspectMeasureInner(SizeFOpt known, SizeAvail, void* nodeContext) {
    AspectMeasureCtx* m = (AspectMeasureCtx*)nodeContext;
    float w = IsSome(known.w) ? known.w : m->width;
    float h = IsSome(known.h) ? known.h : w * m->heightRatio;
    return {w, h};
}

static taffy::LayoutOutput AspectMeasureFn(const taffy::LayoutInput& inputs,
                                           TaffyNodeId node, void* nodeContext,
                                           const taffy::Style* style,
                                           void* userData) {
    (void)node;
    (void)userData;
    return taffy::ComputeLeafLayout(inputs, *style, taffy::CalcResolver{},
                                    AspectMeasureInner, nodeContext);
}

static void InsetAll(taffy::Style* s, float v) {
    taffy::LengthPercentageAuto inset = taffy::LengthPercentageAuto::Length(v);
    s->inset.left = inset;
    s->inset.right = inset;
    s->inset.top = inset;
    s->inset.bottom = inset;
}

// taffy tests/hand_written/absolute_replaced.rs. A replaced absolute box
// uses its intrinsic size instead of stretching between its insets.
static void TestAbsoluteReplacedDoesNotStretch() {
    TestSuite("taffy absolute replaced");
    taffy::Style rootStyle;
    rootStyle.display = taffy::Display::Block;
    rootStyle.position = taffy::Position::Relative;
    rootStyle.size = SizeDim::FromLengths(400.0f, 300.0f);

    {
        TaffyTree tree;
        tree.Init();
        AspectMeasureCtx image = {100.0f, 0.5f};
        taffy::Style absStyle;
        absStyle.position = taffy::Position::Absolute;
        absStyle.itemIsReplaced = true;
        InsetAll(&absStyle, 10.0f);
        TaffyNodeId abs = tree.NewLeafWithContext(absStyle, &image);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &abs, 1);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      AspectMeasureFn, nullptr);
        AssertBox(tree.GetLayout(abs), 10.0f, 10.0f, 100.0f, 50.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        AspectMeasureCtx image = {100.0f, 0.5f};
        taffy::Style absStyle;
        absStyle.position = taffy::Position::Absolute;
        absStyle.itemIsReplaced = true;
        absStyle.size.width = taffy::Dimension::Length(200.0f);
        InsetAll(&absStyle, 10.0f);
        TaffyNodeId abs = tree.NewLeafWithContext(absStyle, &image);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &abs, 1);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      AspectMeasureFn, nullptr);
        utassertnear(tree.GetLayout(abs).size.w, 200.0f);
        utassertnear(tree.GetLayout(abs).size.h, 100.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style absStyle;
        absStyle.position = taffy::Position::Absolute;
        InsetAll(&absStyle, 10.0f);
        TaffyNodeId abs = tree.NewLeaf(absStyle);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &abs, 1);
        tree.ComputeLayout(root, SizeAvail::MaxContent());
        utassertnear(tree.GetLayout(abs).size.w, 380.0f);
        utassertnear(tree.GetLayout(abs).size.h, 280.0f);
        tree.Free();
    }
}

// taffy e299619a. A form control is compressible (intrinsic block width)
// without being a replaced element (absolute stretch between insets).
static void TestCompressibleReplaced() {
    TestSuite("taffy compressible replaced");
    taffy::Style replaced;
    replaced.itemIsReplaced = true;
    utassert(replaced.IsCompressibleReplaced());
    utassert(replaced.IsReplaced());
    taffy::Style form;
    form.itemIsCompressibleReplaced = true;
    utassert(form.IsCompressibleReplaced());
    utassert(!form.IsReplaced());

    taffy::Style blockRoot;
    blockRoot.display = taffy::Display::Block;
    blockRoot.size.width = taffy::Dimension::Length(400.0f);
    {
        TaffyTree tree;
        tree.Init();
        AspectMeasureCtx input = {150.0f, 20.0f / 150.0f};
        taffy::Style child;
        child.display = taffy::Display::Block;
        child.itemIsCompressibleReplaced = true;
        TaffyNodeId node = tree.NewLeafWithContext(child, &input);
        TaffyNodeId root = tree.NewWithChildren(blockRoot, &node, 1);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      AspectMeasureFn, nullptr);
        utassertnear(tree.GetLayout(node).size.w, 150.0f);
        utassertnear(tree.GetLayout(node).size.h, 20.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        AspectMeasureCtx input = {150.0f, 20.0f / 150.0f};
        taffy::Style child;
        child.display = taffy::Display::Block;
        child.itemIsCompressibleReplaced = true;
        child.justifySelf = taffy::OptAlignSelf(
            taffy::AlignItems{taffy::AlignItemsKeyword::Stretch});
        TaffyNodeId node = tree.NewLeafWithContext(child, &input);
        TaffyNodeId root = tree.NewWithChildren(blockRoot, &node, 1);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      AspectMeasureFn, nullptr);
        utassertnear(tree.GetLayout(node).size.w, 400.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        AspectMeasureCtx input = {150.0f, 20.0f / 150.0f};
        taffy::Style child;
        child.position = taffy::Position::Absolute;
        child.itemIsCompressibleReplaced = true;
        InsetAll(&child, 10.0f);
        TaffyNodeId node = tree.NewLeafWithContext(child, &input);
        taffy::Style rootStyle;
        rootStyle.display = taffy::Display::Block;
        rootStyle.position = taffy::Position::Relative;
        rootStyle.size = SizeDim::FromLengths(400.0f, 300.0f);
        TaffyNodeId root = tree.NewWithChildren(rootStyle, &node, 1);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      AspectMeasureFn, nullptr);
        AssertBox(tree.GetLayout(node), 10.0f, 10.0f, 380.0f, 280.0f);
        tree.Free();
    }
}

// taffy a4dff69e. justify-self / align-self place an absolute box inside
// its static-position rectangle, and inside the inset-modified containing
// block when both insets are set. Default, safe and unsafe disagree once
// the margin box overflows that block.
static void TestAbsposSelfAlignment() {
    TestSuite("taffy abspos self alignment");
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style abs = OofLeaf(40.0f, 10.0f);
        abs.position = taffy::Position::Absolute;
        abs.justifySelf = taffy::OptAlignSelf(
            taffy::AlignItems{taffy::AlignItemsKeyword::End});
        TaffyNodeId absNode = tree.NewLeaf(abs);
        taffy::Style flow;
        flow.size.height = taffy::Dimension::Length(20.0f);
        TaffyNodeId flowNode = tree.NewLeaf(flow);
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.position = taffy::Position::Relative;
        root.size = SizeDim::FromLengths(200.0f, 100.0f);
        root.alignContent =
            taffy::AlignContent{taffy::AlignContentKeyword::End};
        TaffyNodeId kids[2] = {absNode, flowNode};
        TaffyNodeId node = tree.NewWithChildren(root, kids, 2);
        tree.ComputeLayout(node, SizeAvail::MaxContent());
        // Content box is the border box. End puts the box at the inline
        // end. align-content shifts the static line down with the in-flow
        // child (free space 80).
        AssertBox(tree.GetLayout(absNode), 160.0f, 80.0f, 40.0f, 10.0f);
        AssertLoc(tree, flowNode, 0.0f, 80.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style abs = OofLeaf(40.0f, 20.0f);
        abs.position = taffy::Position::Absolute;
        abs.justifySelf = taffy::OptAlignSelf(
            taffy::AlignItems{taffy::AlignItemsKeyword::Center});
        abs.inset.left = taffy::LengthPercentageAuto::Length(10.0f);
        abs.inset.right = taffy::LengthPercentageAuto::Length(10.0f);
        abs.inset.top = taffy::LengthPercentageAuto::Length(0.0f);
        TaffyNodeId absNode = tree.NewLeaf(abs);
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.position = taffy::Position::Relative;
        root.size = SizeDim::FromLengths(200.0f, 100.0f);
        TaffyNodeId node = tree.NewWithChildren(root, &absNode, 1);
        tree.ComputeLayout(node, SizeAvail::MaxContent());
        // IMCB is 10..190. The 40px box is centered in it, not at the
        // left inset.
        AssertBox(tree.GetLayout(absNode), 80.0f, 0.0f, 40.0f, 20.0f);
        tree.Free();
    }
    auto overflowAt = [](taffy::AlignmentSafety safety) {
        TaffyTree tree;
        tree.Init();
        taffy::Style abs = OofLeaf(150.0f, 10.0f);
        abs.position = taffy::Position::Absolute;
        abs.justifySelf = taffy::OptAlignSelf(
            taffy::AlignItems{taffy::AlignItemsKeyword::End, safety});
        abs.inset.left = taffy::LengthPercentageAuto::Length(10.0f);
        abs.inset.right = taffy::LengthPercentageAuto::Length(10.0f);
        TaffyNodeId absNode = tree.NewLeaf(abs);
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.position = taffy::Position::Relative;
        root.size = SizeDim::FromLengths(100.0f, 100.0f);
        TaffyNodeId node = tree.NewWithChildren(root, &absNode, 1);
        tree.ComputeLayout(node, SizeAvail::MaxContent());
        float x = tree.GetLayout(absNode).location.x;
        tree.Free();
        return x;
    };
    // IMCB is 10..90. The 150px box overflows it.
    utassertnear(overflowAt(taffy::AlignmentSafety::Unsafe), -60.0f);
    utassertnear(overflowAt(taffy::AlignmentSafety::Safe), 10.0f);
    utassertnear(overflowAt(taffy::AlignmentSafety::Default), 0.0f);

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style abs;
        abs.position = taffy::Position::Absolute;
        abs.itemIsTable = true;
        InsetAll(&abs, 10.0f);
        TaffyNodeId absNode = tree.NewLeaf(abs);
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.position = taffy::Position::Relative;
        root.size = SizeDim::FromLengths(400.0f, 300.0f);
        TaffyNodeId node = tree.NewWithChildren(root, &absNode, 1);
        tree.ComputeLayout(node, SizeAvail::MaxContent());
        // normal does not stretch a table between its insets.
        utassertnear(tree.GetLayout(absNode).size.w, 0.0f);
        utassertnear(tree.GetLayout(absNode).size.h, 0.0f);
        tree.Free();
    }
}

// taffy tests/hand_written/initial_containing_block.rs. Boxes with no
// nearer containing block are positioned against the viewport.
static void TestInitialContainingBlock() {
    TestSuite("taffy initial containing block");
    SizeAvail viewport = {AvailableSpace::Definite(800.0f),
                          AvailableSpace::Definite(600.0f)};
    auto rootStyle = []() {
        taffy::Style s;
        s.display = taffy::Display::Block;
        s.margin.left = taffy::LengthPercentageAuto::Length(10.0f);
        s.margin.right = taffy::LengthPercentageAuto::Length(10.0f);
        s.margin.top = taffy::LengthPercentageAuto::Length(20.0f);
        s.margin.bottom = taffy::LengthPercentageAuto::Length(20.0f);
        s.border.left = taffy::LengthPercentage::Length(5.0f);
        s.border.right = taffy::LengthPercentage::Length(5.0f);
        s.border.top = taffy::LengthPercentage::Length(5.0f);
        s.border.bottom = taffy::LengthPercentage::Length(5.0f);
        return s;
    };

    {
        TaffyTree tree;
        tree.Init();
        TaffyNodeId root = tree.NewLeaf(rootStyle());
        tree.ComputeLayout(root, viewport);
        AssertLoc(tree, root, 10.0f, 20.0f);
        utassertnear(tree.GetLayout(root).size.w, 780.0f);
        tree.Free();
    }

    taffy::Position positions[] = {taffy::Position::Absolute,
                                   taffy::Position::Fixed};
    for (taffy::Position position : positions) {
        TaffyTree tree;
        tree.Init();
        taffy::Style oof;
        oof.position = position;
        InsetAll(&oof, 0.0f);
        TaffyNodeId child = tree.NewLeaf(oof);
        TaffyNodeId root = tree.NewWithChildren(rootStyle(), &child, 1);
        tree.ComputeLayout(root, viewport);
        AssertBox(tree.GetLayout(child), -10.0f, -20.0f, 800.0f, 600.0f);
        tree.Free();
    }
    for (taffy::Position position : positions) {
        TaffyTree tree;
        tree.Init();
        taffy::Style oof = OofLeaf(50.0f, 50.0f);
        oof.position = position;
        oof.inset.right = taffy::LengthPercentageAuto::Length(0.0f);
        oof.inset.bottom = taffy::LengthPercentageAuto::Length(0.0f);
        TaffyNodeId child = tree.NewLeaf(oof);
        TaffyNodeId root = tree.NewWithChildren(rootStyle(), &child, 1);
        tree.ComputeLayout(root, viewport);
        AssertLoc(tree, child, 740.0f, 530.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style oof = OofLeaf(50.0f, 50.0f);
        oof.position = taffy::Position::Absolute;
        TaffyNodeId child = tree.NewLeaf(oof);
        TaffyNodeId root = tree.NewWithChildren(rootStyle(), &child, 1);
        tree.ComputeLayout(root, viewport);
        AssertLoc(tree, child, 5.0f, 5.0f);
        tree.Free();
    }
}

// taffy fb461a78. A relative root is shifted by its insets. An absolute or
// fixed root with a definite viewport is positioned like any other
// out-of-flow box. An indefinite viewport leaves that root in flow.
static void TestRootPositionAndInsets() {
    TestSuite("taffy root position");
    SizeAvail viewport = {AvailableSpace::Definite(800.0f),
                          AvailableSpace::Definite(600.0f)};
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.position = taffy::Position::Fixed;
        root.inset.top = taffy::LengthPercentageAuto::Percent(0.1f);
        root.inset.right = taffy::LengthPercentageAuto::Length(10.0f);
        root.size = SizeDim::FromLengths(100.0f, 50.0f);
        TaffyNodeId node = tree.NewLeaf(root);
        tree.ComputeLayout(node, viewport);
        AssertBox(tree.GetLayout(node), 690.0f, 60.0f, 100.0f, 50.0f);
        tree.ComputeLayout(node, viewport);
        AssertBox(tree.GetLayout(node), 690.0f, 60.0f, 100.0f, 50.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.position = taffy::Position::Fixed;
        root.inset.top = taffy::LengthPercentageAuto::Length(10.0f);
        root.inset.left = taffy::LengthPercentageAuto::Length(10.0f);
        root.size = SizeDim::FromLengths(100.0f, 50.0f);
        TaffyNodeId node = tree.NewLeaf(root);
        tree.ComputeLayout(node, SizeAvail::MaxContent());
        AssertBox(tree.GetLayout(node), 0.0f, 0.0f, 100.0f, 50.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.position = taffy::Position::Relative;
        root.inset.left = taffy::LengthPercentageAuto::Length(25.0f);
        root.inset.bottom = taffy::LengthPercentageAuto::Length(15.0f);
        root.size = SizeDim::FromLengths(100.0f, 50.0f);
        root.margin.left = taffy::LengthPercentageAuto::Length(10.0f);
        root.margin.right = taffy::LengthPercentageAuto::Length(10.0f);
        root.margin.top = taffy::LengthPercentageAuto::Length(10.0f);
        root.margin.bottom = taffy::LengthPercentageAuto::Length(10.0f);
        root.border.left = taffy::LengthPercentage::Length(5.0f);
        root.border.right = taffy::LengthPercentage::Length(5.0f);
        root.border.top = taffy::LengthPercentage::Length(5.0f);
        root.border.bottom = taffy::LengthPercentage::Length(5.0f);
        taffy::Style flow;
        flow.size.height = taffy::Dimension::Length(10.0f);
        TaffyNodeId flowNode = tree.NewLeaf(flow);
        taffy::Style abs = OofLeaf(20.0f, 20.0f);
        abs.position = taffy::Position::Absolute;
        abs.inset.right = taffy::LengthPercentageAuto::Length(0.0f);
        abs.inset.bottom = taffy::LengthPercentageAuto::Length(10.0f);
        TaffyNodeId absNode = tree.NewLeaf(abs);
        TaffyNodeId kids[2] = {flowNode, absNode};
        TaffyNodeId node = tree.NewWithChildren(root, kids, 2);
        SizeAvail page = {AvailableSpace::Definite(400.0f),
                          AvailableSpace::Definite(300.0f)};
        tree.ComputeLayout(node, page);
        AssertBox(tree.GetLayout(node), 35.0f, -5.0f, 100.0f, 50.0f);
        AssertBox(tree.GetLayout(flowNode), 5.0f, 5.0f, 90.0f, 10.0f);
        AssertBox(tree.GetLayout(absNode), 75.0f, 15.0f, 20.0f, 20.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.position = taffy::Position::Absolute;
        root.inset.top = taffy::LengthPercentageAuto::Length(20.0f);
        root.inset.left = taffy::LengthPercentageAuto::Length(50.0f);
        root.inset.bottom = taffy::LengthPercentageAuto::Length(40.0f);
        root.inset.right = taffy::LengthPercentageAuto::Length(30.0f);
        taffy::Style flow;
        flow.size.height = taffy::Dimension::Length(10.0f);
        TaffyNodeId flowNode = tree.NewLeaf(flow);
        TaffyNodeId node = tree.NewWithChildren(root, &flowNode, 1);
        SizeAvail page = {AvailableSpace::Definite(400.0f),
                          AvailableSpace::Definite(300.0f)};
        tree.ComputeLayout(node, page);
        AssertBox(tree.GetLayout(node), 50.0f, 20.0f, 320.0f, 240.0f);
        AssertBox(tree.GetLayout(flowNode), 0.0f, 0.0f, 320.0f, 10.0f);
        tree.Free();
    }
}

// taffy 8391a347. Percentage padding on a flex item resolves against the
// container's width, including the block-axis sides of a column.
static void TestFlexPercentagePaddingAgainstWidth() {
    TestSuite("taffy flex percentage padding");
    SizeAvail space = SizeAvail::MaxContent();
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style root;
        root.display = taffy::Display::Flex;
        root.flexDirection = taffy::FlexDirection::Column;
        root.size = SizeDim::FromLengths(400.0f, 300.0f);
        taffy::Style item;
        item.flexBasis = taffy::Dimension::Length(50.0f);
        item.padding.top = taffy::LengthPercentage::Percent(0.1f);
        item.padding.bottom = taffy::LengthPercentage::Percent(0.05f);
        TaffyNodeId child = tree.NewLeaf(item);
        TaffyNodeId node = tree.NewWithChildren(root, &child, 1);
        tree.ComputeLayout(node, space);
        AssertBox(tree.GetLayout(child), 0.0f, 0.0f, 400.0f, 60.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style root;
        root.display = taffy::Display::Flex;
        root.boxSizing = taffy::BoxSizing::ContentBox;
        root.flexDirection = taffy::FlexDirection::Column;
        root.size = SizeDim::FromLengths(400.0f, 300.0f);
        taffy::Style item;
        item.boxSizing = taffy::BoxSizing::ContentBox;
        item.flexBasis = taffy::Dimension::Length(50.0f);
        item.padding.top = taffy::LengthPercentage::Percent(0.1f);
        item.padding.bottom = taffy::LengthPercentage::Percent(0.05f);
        TaffyNodeId child = tree.NewLeaf(item);
        TaffyNodeId node = tree.NewWithChildren(root, &child, 1);
        tree.ComputeLayout(node, space);
        AssertBox(tree.GetLayout(child), 0.0f, 0.0f, 400.0f, 110.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style root;
        root.display = taffy::Display::Flex;
        root.boxSizing = taffy::BoxSizing::ContentBox;
        root.flexDirection = taffy::FlexDirection::Column;
        root.size.width = taffy::Dimension::Length(400.0f);
        taffy::Style item;
        item.boxSizing = taffy::BoxSizing::ContentBox;
        item.flexBasis = taffy::Dimension::Length(50.0f);
        item.padding.top = taffy::LengthPercentage::Percent(0.1f);
        TaffyNodeId child = tree.NewLeaf(item);
        TaffyNodeId node = tree.NewWithChildren(root, &child, 1);
        tree.ComputeLayout(node, space);
        AssertBox(tree.GetLayout(node), 0.0f, 0.0f, 400.0f, 90.0f);
        AssertBox(tree.GetLayout(child), 0.0f, 0.0f, 400.0f, 90.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        taffy::Style root;
        root.display = taffy::Display::Flex;
        root.boxSizing = taffy::BoxSizing::ContentBox;
        root.size.width = taffy::Dimension::Length(400.0f);
        taffy::Style tall;
        tall.boxSizing = taffy::BoxSizing::ContentBox;
        tall.size = SizeDim::FromLengths(20.0f, 200.0f);
        taffy::Style item;
        item.boxSizing = taffy::BoxSizing::ContentBox;
        item.size.width = taffy::Dimension::Length(20.0f);
        item.maxSize.height = taffy::Dimension::Length(50.0f);
        item.padding.top = taffy::LengthPercentage::Percent(0.1f);
        TaffyNodeId a = tree.NewLeaf(tall);
        TaffyNodeId b = tree.NewLeaf(item);
        TaffyNodeId kids[2] = {a, b};
        TaffyNodeId node = tree.NewWithChildren(root, kids, 2);
        tree.ComputeLayout(node, space);
        AssertBox(tree.GetLayout(node), 0.0f, 0.0f, 400.0f, 200.0f);
        AssertBox(tree.GetLayout(b), 20.0f, 0.0f, 20.0f, 90.0f);
        tree.Free();
    }
}

// taffy 22f941e1. A column's intrinsic height measures a flex-basis 0 item
// at its max-width, so wrapped text is not measured at the wider percentage.
static void TestFlexColumnMeasuresAtMaxWidth() {
    TestSuite("taffy flex column max-width measure");
    TaffyTree tree;
    tree.Init();
    taffy::Style root;
    root.display = taffy::Display::Flex;
    root.flexDirection = taffy::FlexDirection::Column;
    root.size.width = taffy::Dimension::Length(300.0f);
    taffy::Style item;
    item.flexGrow = 1.0f;
    item.flexShrink = 1.0f;
    item.flexBasis = taffy::Dimension::Length(0.0f);
    item.size.width = taffy::Dimension::Percent(1.0f);
    item.maxSize.width = taffy::Dimension::Length(200.0f);
    TaffyNodeId child = tree.NewLeaf(item);
    TaffyNodeId node = tree.NewWithChildren(root, &child, 1);
    tree.ComputeLayoutWithMeasure(node, SizeAvail::MaxContent(),
                                  AhemTwelveMeasure, nullptr);
    AssertBox(tree.GetLayout(node), 0.0f, 0.0f, 300.0f, 30.0f);
    AssertBox(tree.GetLayout(child), 0.0f, 0.0f, 200.0f, 30.0f);
    tree.Free();
}

// taffy 9770a07a. A width-only grid measure sizes the columns and skips
// the rows, so items are not measured for their height.
static void TestGridSkipsRowsWhenOnlyWidthRequested() {
    TestSuite("taffy grid width-only skips rows");
    TaffyTree tree;
    tree.Init();
    int hits = 0;
    TaffyNodeId child = tree.NewLeafWithContext(taffy::Style{}, &hits);
    GridTemplateComponent col =
        GridTemplateComponent::Single(TrackSizingFunction::Length(100.0f));
    taffy::Style root;
    root.display = taffy::Display::Grid;
    root.gridTemplateColumns = GridTemplateOf(&col, 1);
    TaffyNodeId grid = tree.NewWithChildren(root, &child, 1);
    tree.measureFn = GridWidthSkipMeasure;
    float width = tree.MeasureChildSize(
        grid, SizeFOptNone(), SizeFOptNone(), SizeAvail::MaxContent(),
        taffy::SizingMode::InherentSize, taffy::AbsoluteAxis::Horizontal,
        taffy::LineBool::False());
    utassertnear(width, 100.0f);
    utassert(hits == 0);
    tree.measureFn = nullptr;

    tree.ComputeLayoutWithMeasure(grid, SizeAvail::MaxContent(),
                                  GridWidthSkipMeasure, nullptr);
    AssertBox(tree.GetLayout(grid), 0.0f, 0.0f, 100.0f, 25.0f);
    AssertBox(tree.GetLayout(child), 0.0f, 0.0f, 100.0f, 25.0f);
    tree.Free();
}

// taffy 391e383c. A grid that sizes a flex item asks that flex container
// only for its main size. The leaf is measured 10 times in a row and 11
// times in a column (13 and 12 without the short-circuit) and lays out
// at 50x20 inside a 50px-wide grid.
static void TestFlexSkipsCrossSizeWhenOnlyMainRequested() {
    TestSuite("taffy flex main-only skips cross");
    taffy::FlexDirection dirs[2] = {taffy::FlexDirection::Row,
                                    taffy::FlexDirection::Column};
    int expected[2] = {10, 11};
    for (int i = 0; i < 2; i++) {
        TaffyTree tree;
        tree.Init();
        int hits = 0;
        TaffyNodeId leaf = tree.NewLeafWithContext(taffy::Style{}, &hits);
        taffy::Style flex;
        flex.display = taffy::Display::Flex;
        flex.flexDirection = dirs[i];
        TaffyNodeId flexNode = tree.NewWithChildren(flex, &leaf, 1);
        taffy::Style grid;
        grid.display = taffy::Display::Grid;
        grid.size.width = taffy::Dimension::Length(50.0f);
        TaffyNodeId root = tree.NewWithChildren(grid, &flexNode, 1);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      AhemQuadMeasure, nullptr);
        AssertBox(tree.GetLayout(leaf), 0.0f, 0.0f, 50.0f, 20.0f);
        utassert(hits == expected[i]);
        tree.Free();
    }
}

// taffy 9f02a528. A grid item with a definite width contributes that width
// without being measured. The same leaf is measured 5 times without the
// short-circuit, and 3 times with it. Both boxes are 40x20.
static void TestGridKnownWidthSkipsMeasure() {
    TestSuite("taffy grid known width skips measure");
    TaffyTree tree;
    tree.Init();
    int hits = 0;
    taffy::Style item;
    item.size.width = taffy::Dimension::Length(40.0f);
    TaffyNodeId leaf = tree.NewLeafWithContext(item, &hits);
    taffy::Style grid;
    grid.display = taffy::Display::Grid;
    TaffyNodeId root = tree.NewWithChildren(grid, &leaf, 1);
    tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                  AhemQuadMeasure, nullptr);
    AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 40.0f, 20.0f);
    AssertBox(tree.GetLayout(leaf), 0.0f, 0.0f, 40.0f, 20.0f);
    utassert(hits == 3);
    tree.Free();
}

// taffy 29ad0410. An item in a fixed track is not measured to size that
// track, even when another track in the axis is intrinsic. The leaf is
// measured 5 times without the short-circuit and 3 times with it.
static void TestGridFixedTrackSkipsMeasure() {
    TestSuite("taffy grid fixed track skips measure");
    TaffyTree tree;
    tree.Init();
    int hits = 0;
    taffy::Style item;
    item.justifySelf = taffy::OptAlignSelf(taffy::AlignItems{});
    item.gridColumn.start = GridPlacement::FromLineIndex(1);
    TaffyNodeId leaf = tree.NewLeafWithContext(item, &hits);
    taffy::Style sibling;
    sibling.size.width = taffy::Dimension::Length(50.0f);
    sibling.size.height = taffy::Dimension::Length(50.0f);
    sibling.gridColumn.start = GridPlacement::FromLineIndex(2);
    TaffyNodeId sib = tree.NewLeaf(sibling);
    TaffyNodeId kids[2] = {leaf, sib};
    GridTemplateComponent cols[2] = {
        GridTemplateComponent::Single(TrackSizingFunction::Length(100.0f)),
        GridTemplateComponent::Single(TrackSizingFunction::Auto()),
    };
    taffy::Style grid;
    grid.display = taffy::Display::Grid;
    grid.gridTemplateColumns = GridTemplateOf(cols, 2);
    TaffyNodeId root = tree.NewWithChildren(grid, kids, 2);
    tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                  AhemQuadMeasure, nullptr);
    AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 150.0f, 50.0f);
    AssertBox(tree.GetLayout(leaf), 0.0f, 0.0f, 80.0f, 50.0f);
    utassert(hits == 3);
    tree.Free();
}

// taffy a25e1f76. A span-2 item's automatic minimum looks only at the
// tracks it spans. A 1fr track it does not span must not zero that
// minimum. Columns resolve to 40, 40 and 60 inside a 100px grid, and the
// text lays out at 80x20.
static void TestGridAutoMinIgnoresUnspannedFlex() {
    TestSuite("taffy grid auto min ignores unspanned flex");
    TaffyTree tree;
    tree.Init();
    taffy::Style text;
    text.gridRow.start = GridPlacement::FromLineIndex(1);
    text.gridColumn.start = GridPlacement::FromLineIndex(1);
    text.gridColumn.end = GridPlacement::FromSpan(2);
    TaffyNodeId spanned = tree.NewLeaf(text);
    taffy::Style box;
    box.size.width = taffy::Dimension::Length(60.0f);
    box.size.height = taffy::Dimension::Length(10.0f);
    box.gridRow.start = GridPlacement::FromLineIndex(1);
    box.gridColumn.start = GridPlacement::FromLineIndex(3);
    TaffyNodeId fixed = tree.NewLeaf(box);
    TaffyNodeId kids[2] = {spanned, fixed};
    GridTemplateComponent cols[3] = {
        GridTemplateComponent::Single(TrackSizingFunction::Auto()),
        GridTemplateComponent::Single(TrackSizingFunction::Auto()),
        GridTemplateComponent::Single(TrackSizingFunction::Fr(1.0f)),
    };
    taffy::Style grid;
    grid.display = taffy::Display::Grid;
    grid.size.width = taffy::Dimension::Length(100.0f);
    grid.gridTemplateColumns = GridTemplateOf(cols, 3);
    TaffyNodeId root = tree.NewWithChildren(grid, kids, 2);
    tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                  AhemEightPairMeasure, nullptr);
    AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 100.0f, 20.0f);
    AssertBox(tree.GetLayout(spanned), 0.0f, 0.0f, 80.0f, 20.0f);
    AssertBox(tree.GetLayout(fixed), 80.0f, 0.0f, 60.0f, 10.0f);
    tree.Free();
}

// taffy ceb7443d. Column sizing is re-run when a later item's min-content
// contribution changes, not only the first. Three auto columns: a 10x100
// box, then 50% and 100% heights with aspect ratios 1 and 2. Columns
// resolve to 10, 50 and 200.
static void TestGridRerunColumnSizingMultipleItems() {
    TestSuite("taffy grid rerun column sizing multiple items");
    TaffyTree tree;
    tree.Init();
    taffy::Style fixed;
    fixed.size.width = taffy::Dimension::Length(10.0f);
    fixed.size.height = taffy::Dimension::Length(100.0f);
    fixed.gridRow.start = GridPlacement::FromLineIndex(1);
    fixed.gridColumn.start = GridPlacement::FromLineIndex(1);
    TaffyNodeId a = tree.NewLeaf(fixed);
    taffy::Style half;
    half.size.height = taffy::Dimension::Percent(0.5f);
    half.aspectRatio = Some(1.0f);
    half.justifySelf = taffy::OptAlignSelf(taffy::AlignItems{});
    half.gridRow.start = GridPlacement::FromLineIndex(1);
    half.gridColumn.start = GridPlacement::FromLineIndex(2);
    TaffyNodeId b = tree.NewLeaf(half);
    taffy::Style full;
    full.size.height = taffy::Dimension::Percent(1.0f);
    full.aspectRatio = Some(2.0f);
    full.justifySelf = taffy::OptAlignSelf(taffy::AlignItems{});
    full.gridRow.start = GridPlacement::FromLineIndex(1);
    full.gridColumn.start = GridPlacement::FromLineIndex(3);
    TaffyNodeId c = tree.NewLeaf(full);
    TaffyNodeId kids[3] = {a, b, c};
    GridTemplateComponent cols[3] = {
        GridTemplateComponent::Single(TrackSizingFunction::Auto()),
        GridTemplateComponent::Single(TrackSizingFunction::Auto()),
        GridTemplateComponent::Single(TrackSizingFunction::Auto()),
    };
    GridTemplateComponent row =
        GridTemplateComponent::Single(TrackSizingFunction::Auto());
    taffy::Style grid;
    grid.display = taffy::Display::Grid;
    grid.justifyContent = taffy::JustifyContent{};
    grid.gridTemplateColumns = GridTemplateOf(cols, 3);
    grid.gridTemplateRows = GridTemplateOf(&row, 1);
    TaffyNodeId root = tree.NewWithChildren(grid, kids, 3);
    tree.ComputeLayout(root, SizeAvail::MaxContent());
    AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 260.0f, 100.0f);
    AssertBox(tree.GetLayout(a), 0.0f, 0.0f, 10.0f, 100.0f);
    AssertBox(tree.GetLayout(b), 10.0f, 0.0f, 50.0f, 50.0f);
    AssertBox(tree.GetLayout(c), 60.0f, 0.0f, 200.0f, 100.0f);
    tree.Free();
}

// taffy 04965fc5. A definite available space is the border box. A 20px
// margin laid out into 100px leaves 80px, so ten Ahem pairs wrap to 30px
// instead of staying 100px wide and 20px tall.
static void TestShrinkToFitExcludesMargin() {
    TestSuite("taffy shrink-to-fit excludes margin");
    SizeAvail space;
    space.width = AvailableSpace::Definite(100.0f);
    space.height = AvailableSpace::MaxContent();

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style leaf;
        leaf.display = taffy::Display::Block;
        leaf.margin.left = LengthPercentageAuto::Length(20.0f);
        TaffyNodeId node = tree.NewLeaf(leaf);
        tree.ComputeLayoutWithMeasure(node, space, AhemTenPairMeasure, nullptr);
        AssertBox(tree.GetLayout(node), 20.0f, 0.0f, 80.0f, 30.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style child;
        child.size.width = taffy::Dimension::Length(20.0f);
        child.size.height = taffy::Dimension::Length(10.0f);
        child.flexShrink = 0.0f;
        TaffyNodeId kids[10];
        for (int i = 0; i < 10; i++) {
            kids[i] = tree.NewLeaf(child);
        }
        taffy::Style root;
        root.display = taffy::Display::Flex;
        root.flexWrap = taffy::FlexWrap::Wrap;
        root.margin.left = LengthPercentageAuto::Length(20.0f);
        TaffyNodeId node = tree.NewWithChildren(root, kids, 10);
        tree.ComputeLayout(node, space);
        AssertBox(tree.GetLayout(node), 20.0f, 0.0f, 80.0f, 30.0f);
        AssertBox(tree.GetLayout(kids[0]), 0.0f, 0.0f, 20.0f, 10.0f);
        AssertBox(tree.GetLayout(kids[3]), 60.0f, 0.0f, 20.0f, 10.0f);
        AssertBox(tree.GetLayout(kids[4]), 0.0f, 10.0f, 20.0f, 10.0f);
        AssertBox(tree.GetLayout(kids[9]), 20.0f, 20.0f, 20.0f, 10.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style absStyle;
        absStyle.position = taffy::Position::Absolute;
        absStyle.margin.left = LengthPercentageAuto::Length(20.0f);
        TaffyNodeId abs = tree.NewLeaf(absStyle);
        taffy::Style parent;
        parent.display = taffy::Display::Block;
        parent.position = taffy::Position::Relative;
        parent.size = SizeDim::FromLengths(100.0f, 100.0f);
        TaffyNodeId root = tree.NewWithChildren(parent, &abs, 1);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      AhemTenPairMeasure, nullptr);
        AssertBox(tree.GetLayout(abs), 20.0f, 0.0f, 80.0f, 30.0f);
        tree.Free();
    }

    {
        TaffyTree tree;
        tree.Init();
        taffy::Style item;
        item.justifySelf = taffy::OptAlignSelf(taffy::AlignItems{});
        item.margin.left = LengthPercentageAuto::Length(20.0f);
        TaffyNodeId leaf = tree.NewLeaf(item);
        GridTemplateComponent col =
            GridTemplateComponent::Single(TrackSizingFunction::Length(100.0f));
        GridTemplateComponent row =
            GridTemplateComponent::Single(TrackSizingFunction::Auto());
        taffy::Style grid;
        grid.display = taffy::Display::Grid;
        grid.size.width = taffy::Dimension::Length(100.0f);
        grid.gridTemplateColumns = GridTemplateOf(&col, 1);
        grid.gridTemplateRows = GridTemplateOf(&row, 1);
        TaffyNodeId root = tree.NewWithChildren(grid, &leaf, 1);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      AhemTenPairMeasure, nullptr);
        AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 100.0f, 30.0f);
        AssertBox(tree.GetLayout(leaf), 20.0f, 0.0f, 80.0f, 30.0f);
        tree.Free();
    }
}

// taffy d145df5e. A fit-content(50%) column resolves against the grid's
// own size. That size is indefinite for a shrink-to-fit grid even when the
// available width is a definite 200px, so the column is re-run and the
// item is 30px inside a 60px grid. The same numbers hold for max-content.
static void TestGridPercentRerunIndefiniteContainer() {
    TestSuite("taffy grid percentage re-run");
    AvailableSpace widths[2] = {AvailableSpace::MaxContent(),
                                AvailableSpace::Definite(200.0f)};
    for (int i = 0; i < 2; i++) {
        TaffyTree tree;
        tree.Init();
        TaffyNodeId item = tree.NewLeaf(taffy::Style{});
        GridTemplateComponent col = GridTemplateComponent::Single(
            TrackSizingFunction::FitContent(LengthPercentage::Percent(0.5f)));
        GridTemplateComponent row =
            GridTemplateComponent::Single(TrackSizingFunction::Length(40.0f));
        taffy::Style grid;
        grid.display = taffy::Display::Grid;
        grid.gridTemplateColumns = GridTemplateOf(&col, 1);
        grid.gridTemplateRows = GridTemplateOf(&row, 1);
        TaffyNodeId root = tree.NewWithChildren(grid, &item, 1);
        SizeAvail space;
        space.width = widths[i];
        space.height = AvailableSpace::MaxContent();
        tree.ComputeLayoutWithMeasure(root, space, AhemThreePairMeasure,
                                      nullptr);
        utassertnear(tree.GetLayout(root).size.w, 60.0f);
        utassertnear(tree.GetLayout(item).size.w, 30.0f);
        tree.Free();
    }
}

// taffy 66b33640. Percentage rows re-resolve against an indefinite
// container's height even when no column is re-run. Rows are auto, 20%
// and auto, holding items 20, 60 and 20 tall. The grid is 100px: the
// percentage row is 20px and the auto rows stretch to 40px each. A
// definite available height of 500px does not change that.
static void TestGridPercentRowsIndefiniteContainer() {
    TestSuite("taffy grid percentage rows");
    AvailableSpace heights[2] = {AvailableSpace::MaxContent(),
                                 AvailableSpace::Definite(500.0f)};
    float itemH[3] = {20.0f, 60.0f, 20.0f};
    for (int pass = 0; pass < 2; pass++) {
        TaffyTree tree;
        tree.Init();
        TaffyNodeId kids[3];
        for (int i = 0; i < 3; i++) {
            taffy::Style item;
            item.size.width = taffy::Dimension::Length(20.0f);
            item.size.height = taffy::Dimension::Length(itemH[i]);
            kids[i] = tree.NewLeaf(item);
        }
        GridTemplateComponent col =
            GridTemplateComponent::Single(TrackSizingFunction::Auto());
        GridTemplateComponent rows[3] = {
            GridTemplateComponent::Single(TrackSizingFunction::Auto()),
            GridTemplateComponent::Single(TrackSizingFunction::Percent(0.2f)),
            GridTemplateComponent::Single(TrackSizingFunction::Auto()),
        };
        taffy::Style grid;
        grid.display = taffy::Display::Grid;
        grid.gridTemplateColumns = GridTemplateOf(&col, 1);
        grid.gridTemplateRows = GridTemplateOf(rows, 3);
        TaffyNodeId root = tree.NewWithChildren(grid, kids, 3);
        SizeAvail space;
        space.width = AvailableSpace::Definite(200.0f);
        space.height = heights[pass];
        tree.ComputeLayout(root, space);
        float yb = tree.GetLayout(kids[1]).location.y;
        float yc = tree.GetLayout(kids[2]).location.y;
        utassertnear(tree.GetLayout(root).size.h, 100.0f);
        utassertnear(yb, 40.0f);
        utassertnear(yc - yb, 20.0f);
        utassertnear(100.0f - yc, 40.0f);
        tree.Free();
    }
}

// Ahem 10px words. `lens` is each word's character count. ZWSP separates
// the words, so the min-content width is the longest word.
struct AhemWords {
    const int* lens;
    int count;
};

static SizeF AhemWordsInner(SizeFOpt known, SizeAvail available, void* ctx) {
    const AhemWords* words = (const AhemWords*)ctx;
    const float kH = 10.0f;
    float maxC = 0.0f;
    float minC = 0.0f;
    for (int i = 0; i < words->count; i++) {
        float w = (float)words->lens[i] * kH;
        maxC += w;
        if (w > minC) {
            minC = w;
        }
    }
    float inlineSize;
    if (IsSome(known.w)) {
        inlineSize = known.w;
    } else if (available.width.kind == AvailableSpace::Kind::MinContent) {
        inlineSize = minC;
    } else if (available.width.kind == AvailableSpace::Kind::MaxContent) {
        inlineSize = maxC;
    } else {
        inlineSize = F32Min(available.width.value, maxC);
    }
    if (inlineSize < minC) {
        inlineSize = minC;
    }
    float block;
    if (IsSome(known.h)) {
        block = known.h;
    } else {
        int inlineChars = (int)(inlineSize / kH);
        if (inlineChars < 1) {
            inlineChars = 1;
        }
        int lineCount = 1;
        int current = 0;
        for (int i = 0; i < words->count; i++) {
            int word = words->lens[i];
            if (current > 0 && current + word > inlineChars) {
                lineCount++;
                current = word;
            } else {
                current += word;
            }
        }
        block = (float)lineCount * kH;
    }
    return {inlineSize, block};
}

static taffy::LayoutOutput AhemWordsMeasure(const taffy::LayoutInput& inputs,
                                            TaffyNodeId node, void* nodeContext,
                                            const taffy::Style* style,
                                            void* userData) {
    (void)node;
    (void)userData;
    return taffy::ComputeLeafLayout(inputs, *style, taffy::CalcResolver{},
                                    AhemWordsInner, nodeContext);
}

// taffy 3defa72e. A spanning item's max-content contribution grows only
// tracks whose growth limit is still infinite. The fit-content(40px)
// track was already fixed at 20px by "XX", so the span grows the other
// track: columns are 45px and 20px inside a 100px grid with a 5px gap.
static void TestGridFitContentGrowthLimit() {
    TestSuite("taffy grid fit-content growth limit");
    {
        TaffyTree tree;
        tree.Init();
        static const int kXx[] = {2};
        static const int kSpan[] = {3, 3, 1};
        AhemWords xx = {kXx, 1};
        AhemWords spanWords = {kSpan, 3};
        taffy::Style narrow;
        narrow.gridRow.start = GridPlacement::FromLineIndex(1);
        narrow.gridColumn.start = GridPlacement::FromLineIndex(2);
        TaffyNodeId a = tree.NewLeafWithContext(narrow, &xx);
        taffy::Style span;
        span.gridRow.start = GridPlacement::FromLineIndex(2);
        span.gridColumn.start = GridPlacement::FromLineIndex(1);
        span.gridColumn.end = GridPlacement::FromLineIndex(-1);
        TaffyNodeId b = tree.NewLeafWithContext(span, &spanWords);
        TaffyNodeId kids[2] = {a, b};
        GridTemplateComponent cols[2] = {
            GridTemplateComponent::Single(TrackSizingFunction::FitContent(
                LengthPercentage::Length(110.0f))),
            GridTemplateComponent::Single(TrackSizingFunction::FitContent(
                LengthPercentage::Length(40.0f))),
        };
        taffy::Style grid;
        grid.display = taffy::Display::Grid;
        grid.size.width = taffy::Dimension::Length(100.0f);
        grid.gap.width = LengthPercentage::Length(5.0f);
        grid.justifyContent = taffy::JustifyContent{};
        grid.gridTemplateColumns = GridTemplateOf(cols, 2);
        TaffyNodeId root = tree.NewWithChildren(grid, kids, 2);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      AhemWordsMeasure, nullptr);
        AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 100.0f, 20.0f);
        AssertBox(tree.GetLayout(a), 50.0f, 0.0f, 20.0f, 10.0f);
        AssertBox(tree.GetLayout(b), 0.0f, 10.0f, 70.0f, 10.0f);
        tree.Free();
    }

    // A fit-content(110px) track between two auto tracks takes an equal
    // share of a spanning max-content contribution, because that share is
    // under the fit-content() argument. The 110px text wraps to 20px.
    {
        TaffyTree tree;
        tree.Init();
        static const int kLong[] = {2, 2, 2, 2, 2, 1};
        static const int kShort[] = {3, 3, 1};
        AhemWords longWords = {kLong, 6};
        AhemWords shortWords = {kShort, 3};
        taffy::Style first;
        first.gridRow.start = GridPlacement::FromLineIndex(1);
        first.gridColumn.start = GridPlacement::FromLineIndex(1);
        first.gridColumn.end = GridPlacement::FromLineIndex(-1);
        TaffyNodeId a = tree.NewLeafWithContext(first, &longWords);
        taffy::Style second;
        second.gridRow.start = GridPlacement::FromLineIndex(2);
        second.gridColumn.start = GridPlacement::FromLineIndex(1);
        second.gridColumn.end = GridPlacement::FromLineIndex(-1);
        TaffyNodeId b = tree.NewLeafWithContext(second, &shortWords);
        TaffyNodeId kids[2] = {a, b};
        GridTemplateComponent cols[3] = {
            GridTemplateComponent::Single(TrackSizingFunction::Auto()),
            GridTemplateComponent::Single(TrackSizingFunction::FitContent(
                LengthPercentage::Length(110.0f))),
            GridTemplateComponent::Single(TrackSizingFunction::Auto()),
        };
        taffy::Style grid;
        grid.display = taffy::Display::Grid;
        grid.size.width = taffy::Dimension::Length(100.0f);
        grid.gap.width = LengthPercentage::Length(5.0f);
        grid.justifyContent = taffy::JustifyContent{};
        grid.gridTemplateColumns = GridTemplateOf(cols, 3);
        TaffyNodeId root = tree.NewWithChildren(grid, kids, 2);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      AhemWordsMeasure, nullptr);
        AssertBox(tree.GetLayout(root), 0.0f, 0.0f, 100.0f, 30.0f);
        AssertBox(tree.GetLayout(a), 0.0f, 0.0f, 100.0f, 20.0f);
        AssertBox(tree.GetLayout(b), 0.0f, 20.0f, 100.0f, 10.0f);
        tree.Free();
    }
}

// A calc() of `pct * basis + px`. 8-byte aligned so the address can be tagged.
struct FitCalc {
    float px;
    float pct;
};

static float FitCalcResolve(const void* handle, float basis, void*) {
    const FitCalc* c = (const FitCalc*)handle;
    return c->pct * basis + c->px;
}

struct FitLeaf {
    float minWidth;
    float maxWidth;
    float height;
};

static SizeF FitLeafInner(SizeFOpt known, SizeAvail space, void* ctx) {
    FitLeaf* leaf = (FitLeaf*)ctx;
    float width;
    if (IsSome(known.w)) {
        width = known.w;
    } else if (space.width.kind == AvailableSpace::Kind::MinContent) {
        width = leaf->minWidth;
    } else if (space.width.kind == AvailableSpace::Kind::MaxContent) {
        width = leaf->maxWidth;
    } else {
        width =
            F32Min(leaf->maxWidth, F32Max(leaf->minWidth, space.width.value));
    }
    float height = IsSome(known.h) ? known.h : leaf->height;
    return {width, height};
}

static taffy::LayoutOutput FitLeafMeasure(const taffy::LayoutInput& inputs,
                                          TaffyNodeId node, void* nodeContext,
                                          const taffy::Style* style,
                                          void* userData) {
    (void)node;
    (void)userData;
    taffy::CalcResolver resolver;
    resolver.fn = FitCalcResolve;
    return taffy::ComputeLeafLayout(inputs, *style, resolver, FitLeafInner,
                                    nodeContext);
}

// taffy 49152ca2 tests/hand_written/fit_content_calc.rs.
static void TestFitContentCalc() {
    TestSuite("taffy fit-content calc");
    bool ownArena = gGridArena == nullptr;
    if (ownArena) {
        gGridArena = gpui::ArenaNew();
    }

    alignas(8) FitCalc rowCalc{5.0f, 0.1f};
    {
        TaffyTree tree;
        tree.Init();
        tree.calc.fn = FitCalcResolve;
        taffy::Style item;
        item.minSize.height = taffy::Dimension::Length(0.0f);
        FitLeaf leaf{0.0f, 0.0f, 400.0f};
        TaffyNodeId child = tree.NewLeafWithContext(item, &leaf);
        GridTemplateComponent row = GridTemplateComponent::Single(
            TrackSizingFunction::FitContent(LengthPercentage::Calc(&rowCalc)));
        taffy::Style grid;
        grid.display = taffy::Display::Grid;
        grid.size.width = taffy::Dimension::Length(100.0f);
        grid.size.height = taffy::Dimension::Length(300.0f);
        grid.gridTemplateRows = GridTemplateOf(&row, 1);
        TaffyNodeId root = tree.NewWithChildren(grid, &child, 1);
        tree.ComputeLayoutWithMeasure(root, SizeAvail::MaxContent(),
                                      FitLeafMeasure, nullptr);
        utassertnear(tree.GetLayout(child).size.h, 35.0f);
        tree.Free();
    }

    alignas(8) FitCalc widthCalc{70.0f, 0.1f};
    {
        TaffyTree tree;
        tree.Init();
        tree.calc.fn = FitCalcResolve;
        taffy::Style childStyle;
        childStyle.size.width = taffy::Dimension::FitContentCalc(&widthCalc);
        FitLeaf leaf{50.0f, 400.0f, 10.0f};
        TaffyNodeId child = tree.NewLeafWithContext(childStyle, &leaf);
        taffy::Style root;
        root.display = taffy::Display::Block;
        root.size.width = taffy::Dimension::Length(300.0f);
        TaffyNodeId node = tree.NewWithChildren(root, &child, 1);
        tree.ComputeLayoutWithMeasure(node, SizeAvail::MaxContent(),
                                      FitLeafMeasure, nullptr);
        utassertnear(tree.GetLayout(child).size.w, 100.0f);
        tree.Free();
    }

    auto column = [](TrackSizingFunction col, taffy::Dimension width,
                     AvailableSpace availW, float* containerW, float* itemW) {
        TaffyTree tree;
        tree.Init();
        tree.calc.fn = FitCalcResolve;
        FitLeaf leaf{50.0f, 150.0f, 10.0f};
        TaffyNodeId child = tree.NewLeafWithContext(taffy::Style{}, &leaf);
        GridTemplateComponent comp = GridTemplateComponent::Single(col);
        taffy::Style grid;
        grid.display = taffy::Display::Grid;
        grid.size.width = width;
        grid.size.height = taffy::Dimension::Length(40.0f);
        grid.gridTemplateColumns = GridTemplateOf(&comp, 1);
        TaffyNodeId root = tree.NewWithChildren(grid, &child, 1);
        SizeAvail space;
        space.width = availW;
        space.height = AvailableSpace::MaxContent();
        tree.ComputeLayoutWithMeasure(root, space, FitLeafMeasure, nullptr);
        *containerW = tree.GetLayout(root).size.w;
        *itemW = tree.GetLayout(child).size.w;
        tree.Free();
    };

    alignas(8) FitCalc fifty{10.0f, 0.2f};
    float calcContainer = 0.0f;
    float calcItem = 0.0f;
    float pxContainer = 0.0f;
    float pxItem = 0.0f;
    column(TrackSizingFunction::FitContent(LengthPercentage::Calc(&fifty)),
           taffy::Dimension::Length(200.0f), AvailableSpace::MaxContent(),
           &calcContainer, &calcItem);
    column(TrackSizingFunction::FitContent(LengthPercentage::Length(50.0f)),
           taffy::Dimension::Length(200.0f), AvailableSpace::MaxContent(),
           &pxContainer, &pxItem);
    utassertnear(calcContainer, pxContainer);
    utassertnear(calcItem, pxItem);
    utassertnear(calcItem, 50.0f);

    alignas(8) FitCalc half{0.0f, 0.5f};
    AvailableSpace widths[3] = {AvailableSpace::Definite(200.0f),
                                AvailableSpace::MaxContent(),
                                AvailableSpace::MinContent()};
    for (int i = 0; i < 3; i++) {
        float calcW = 0.0f;
        float calcI = 0.0f;
        float pctW = 0.0f;
        float pctI = 0.0f;
        column(TrackSizingFunction::FitContent(LengthPercentage::Calc(&half)),
               taffy::Dimension::Auto(), widths[i], &calcW, &calcI);
        column(TrackSizingFunction::FitContent(LengthPercentage::Percent(0.5f)),
               taffy::Dimension::Auto(), widths[i], &pctW, &pctI);
        utassertnear(calcW, pctW);
        utassertnear(calcI, pctI);
    }
    float containerW = 0.0f;
    float itemW = 0.0f;
    column(TrackSizingFunction::FitContent(LengthPercentage::Calc(&half)),
           taffy::Dimension::Auto(), AvailableSpace::MaxContent(), &containerW,
           &itemW);
    utassertnear(containerW, 150.0f);
    utassertnear(itemW, 75.0f);

    if (ownArena) {
        gpui::ArenaDelete(gGridArena);
        gGridArena = nullptr;
    }
}

// taffy e1fd88d2. normal is the items-alignment default, and a flex item
// treats it as stretch.
static void TestAlignItemsNormal() {
    TestSuite("taffy align-items normal");
    TaffyTree tree;
    tree.Init();
    taffy::Style child;
    child.size.width = taffy::Dimension::Length(10.0f);
    TaffyNodeId node = tree.NewLeaf(child);
    taffy::Style root;
    root.display = taffy::Display::Flex;
    root.size = SizeDim::FromLengths(100.0f, 50.0f);
    root.alignItems = AlignItems{AlignItemsKeyword::Normal};
    TaffyNodeId parent = tree.NewWithChildren(root, &node, 1);
    tree.ComputeLayout(parent, SizeAvail::MaxContent());
    utassertnear(tree.GetLayout(node).size.h, 50.0f);
    tree.Free();

    // normal justify-content is flex-start, so a reversed row packs at the end.
    tree.Init();
    taffy::Style revChild;
    revChild.size = SizeDim::FromLengths(10.0f, 10.0f);
    TaffyNodeId revNode = tree.NewLeaf(revChild);
    taffy::Style revRoot;
    revRoot.display = taffy::Display::Flex;
    revRoot.flexDirection = taffy::FlexDirection::RowReverse;
    revRoot.size = SizeDim::FromLengths(100.0f, 20.0f);
    revRoot.justifyContent = AlignContent{AlignContentKeyword::Normal};
    TaffyNodeId revParent = tree.NewWithChildren(revRoot, &revNode, 1);
    tree.ComputeLayout(revParent, SizeAvail::MaxContent());
    utassertnear(tree.GetLayout(revNode).location.x, 90.0f);
    tree.Free();
}

// taffy 2a9aa3bd. align-content end still counts a nested float toward
// the content height, so the stack is not dropped to the padding edge.
static void TestBlockAlignContentNestedFloat() {
    TestSuite("taffy block align-content nested float");
    TaffyTree tree;
    tree.Init();
    taffy::Style floated;
    floated.display = taffy::Display::Block;
    floated.floatMode = taffy::Float::Right;
    floated.size = SizeDim::FromLengths(50.0f, 60.0f);
    TaffyNodeId floatNode = tree.NewLeaf(floated);
    taffy::Style host;
    host.display = taffy::Display::Block;
    host.size = SizeDim::FromLengths(200.0f, 20.0f);
    TaffyNodeId hostNode = tree.NewWithChildren(host, &floatNode, 1);
    taffy::Style next;
    next.display = taffy::Display::Block;
    next.overflow = {taffy::Overflow::Hidden, taffy::Overflow::Hidden};
    next.size = SizeDim::FromLengths(100.0f, 20.0f);
    TaffyNodeId nextNode = tree.NewLeaf(next);
    TaffyNodeId kids[2] = {hostNode, nextNode};
    taffy::Style root;
    root.display = taffy::Display::Block;
    root.alignContent = AlignContent{AlignContentKeyword::End};
    root.size = SizeDim::FromLengths(200.0f, 200.0f);
    TaffyNodeId node = tree.NewWithChildren(root, kids, 2);
    tree.ComputeLayout(node, SizeAvail::MaxContent());
    AssertBox(tree.GetLayout(hostNode), 0.0f, 140.0f, 200.0f, 20.0f);
    AssertBox(tree.GetLayout(floatNode), 150.0f, 0.0f, 50.0f, 60.0f);
    AssertBox(tree.GetLayout(nextNode), 0.0f, 160.0f, 100.0f, 20.0f);
    tree.Free();
}

// taffy 4142c9d8. A percentage min size on an auto track is cyclic, so
// the minimum contribution treats the indefinite area as zero. The
// grid's own 50px size wins over the 100px grandchild.
// An indefinite container treats minmax(percent, fixed) as auto on the min
// side, so the fixed max clamps the item. Treating the percentage as
// min-content would grow the track to the item's content size.
static void TestGridIndefinitePercentMinTrack() {
    TestSuite("taffy grid indefinite percent min track");
    {
        TaffyTree tree;
        tree.Init();
        SizeF content = {30.0f, 10.0f};
        TaffyNodeId child = tree.NewLeafWithContext(taffy::Style{}, &content);
        GridTemplateComponent col = GridTemplateComponent::Single(
            TrackSizingFunction::MinMax(MinTrackSizingFunction::Percent(0.2f),
                                        MaxTrackSizingFunction::Length(20.0f)));
        taffy::Style root;
        root.display = taffy::Display::Grid;
        root.gridTemplateColumns = GridTemplateOf(&col, 1);
        TaffyNodeId rootNode = tree.NewWithChildren(root, &child, 1);
        tree.ComputeLayoutWithMeasure(rootNode, SizeAvail::MaxContent(),
                                      SizeMeasureFunction, nullptr);
        AssertBox(tree.GetLayout(rootNode), 0.0f, 0.0f, 20.0f, 10.0f);
        AssertBox(tree.GetLayout(child), 0.0f, 0.0f, 20.0f, 10.0f);
        tree.Free();
    }
    {
        TaffyTree tree;
        tree.Init();
        SizeF content = {20.0f, 40.0f};
        TaffyNodeId child = tree.NewLeafWithContext(taffy::Style{}, &content);
        GridTemplateComponent col =
            GridTemplateComponent::Single(TrackSizingFunction::Length(20.0f));
        GridTemplateComponent row = GridTemplateComponent::Single(
            TrackSizingFunction::MinMax(MinTrackSizingFunction::Percent(1.0f),
                                        MaxTrackSizingFunction::Length(15.0f)));
        taffy::Style root;
        root.display = taffy::Display::Grid;
        root.gridTemplateColumns = GridTemplateOf(&col, 1);
        root.gridTemplateRows = GridTemplateOf(&row, 1);
        TaffyNodeId rootNode = tree.NewWithChildren(root, &child, 1);
        tree.ComputeLayoutWithMeasure(rootNode, SizeAvail::MaxContent(),
                                      SizeMeasureFunction, nullptr);
        AssertBox(tree.GetLayout(rootNode), 0.0f, 0.0f, 20.0f, 15.0f);
        AssertBox(tree.GetLayout(child), 0.0f, 0.0f, 20.0f, 15.0f);
        tree.Free();
    }
}

// Sparse packing of a row-locked item continues after auto-placed items
// that started on that row. An item that only spans into the row does not
// move the cursor, so a later item can still use the free cell in front.
static void TestGridSparseSpanningLockedItems() {
    TestSuite("taffy grid sparse spanning locked items");
    Direction directions[2] = {Direction::Ltr, Direction::Rtl};
    float xs[2][8] = {
        {0.0f, 50.0f, 100.0f, 50.0f, 100.0f, 0.0f, 150.0f, 0.0f},
        {150.0f, 100.0f, 50.0f, 100.0f, 50.0f, 150.0f, 0.0f, 150.0f}};
    float ys[8] = {20.0f, 40.0f, 20.0f, 0.0f, 0.0f, 40.0f, 20.0f, 0.0f};
    float hs[8] = {20.0f, 20.0f, 40.0f, 40.0f, 20.0f, 20.0f, 20.0f, 20.0f};
    for (int d = 0; d < 2; d++) {
        TaffyTree tree;
        tree.Init();
        taffy::Style styles[8];
        styles[0].gridRow.start = GLine(2);
        styles[0].gridColumn.start = GLine(1);
        styles[1].gridRow.start = GLine(3);
        styles[1].gridColumn.start = GLine(2);
        styles[2].gridRow = {GLine(2), GSpan(2)};
        styles[3].gridRow = {GLine(1), GSpan(2)};
        styles[4].gridRow.start = GLine(1);
        styles[5].gridRow.start = GLine(3);
        styles[6].gridRow.start = GLine(2);
        TaffyNodeId kids[8];
        for (int i = 0; i < 8; i++) {
            styles[i].direction = directions[d];
            kids[i] = tree.NewLeaf(styles[i]);
        }
        TrackSizingFunction col = TrackSizingFunction::Length(50.0f);
        TrackSizingFunction row = TrackSizingFunction::Length(20.0f);
        taffy::Style root;
        root.display = taffy::Display::Grid;
        root.direction = directions[d];
        root.gridAutoColumns = SliceDup(gGridArena, &col, 1);
        root.gridAutoRows = SliceDup(gGridArena, &row, 1);
        TaffyNodeId rootNode = tree.NewWithChildren(root, kids, 8);
        tree.ComputeLayout(rootNode, SizeAvail::MaxContent());
        AssertBox(tree.GetLayout(rootNode), 0.0f, 0.0f, 200.0f, 60.0f);
        for (int i = 0; i < 8; i++) {
            AssertBox(tree.GetLayout(kids[i]), xs[d][i], ys[i], 50.0f, hs[i]);
        }
        tree.Free();
    }
}

static void TestGridCyclicPercentMinContribution() {
    TestSuite("taffy grid cyclic percent min");
    for (int withPreferred = 0; withPreferred < 2; withPreferred++) {
        TaffyTree tree;
        tree.Init();
        taffy::Style grand;
        grand.size = SizeDim::FromLengths(100.0f, 100.0f);
        TaffyNodeId grandNode = tree.NewLeaf(grand);
        taffy::Style mid;
        mid.display = taffy::Display::Grid;
        mid.minSize = SizeDim::FromPercent(1.0f, 1.0f);
        if (withPreferred) {
            mid.size = SizeDim::FromPercent(1.0f, 1.0f);
        }
        TaffyNodeId midNode = tree.NewWithChildren(mid, &grandNode, 1);
        GridTemplateComponent track =
            GridTemplateComponent::Single(TrackSizingFunction::Auto());
        taffy::Style root;
        root.display = taffy::Display::Grid;
        root.size = SizeDim::FromLengths(50.0f, 50.0f);
        root.gridTemplateColumns = GridTemplateOf(&track, 1);
        root.gridTemplateRows = GridTemplateOf(&track, 1);
        TaffyNodeId rootNode = tree.NewWithChildren(root, &midNode, 1);
        tree.ComputeLayout(rootNode, SizeAvail::MaxContent());
        AssertBox(tree.GetLayout(rootNode), 0.0f, 0.0f, 50.0f, 50.0f);
        AssertBox(tree.GetLayout(midNode), 0.0f, 0.0f, 50.0f, 50.0f);
        AssertBox(tree.GetLayout(grandNode), 0.0f, 0.0f, 100.0f, 100.0f);
        tree.Free();
    }
}

void TestTaffy() {
    TestScrollColumnDoesNotShrink();
    TestMaybeMathOptOpt();
    TestMaybeMathOptFloat();
    TestMaybeMathFloatOpt();
    TestMaybeResolveDimension();
    TestMaybeResolveSizeDimension();
    TestResolveOrZeroDimension();
    TestResolveOrZeroRect();
    TestAlignment();
    TestAlignItemsNormal();
    TestBlockAlignContentNestedFloat();
    TestFlexDirection();
    TestStyleDefaults();
    TestPositionStaticSticky();
    TestResolveStaticOffset();
    TestOofHoisting();
    TestAbsposAutoMarginNeedsBothInsets();
    TestAbsoluteReplacedDoesNotStretch();
    TestCompressibleReplaced();
    TestAbsposSelfAlignment();
    TestInitialContainingBlock();
    TestRootPositionAndInsets();
    TestFlexPercentagePaddingAgainstWidth();
    TestFlexColumnMeasuresAtMaxWidth();
    TestGridSkipsRowsWhenOnlyWidthRequested();
    TestFlexSkipsCrossSizeWhenOnlyMainRequested();
    TestGridKnownWidthSkipsMeasure();
    TestGridFixedTrackSkipsMeasure();
    TestGridAutoMinIgnoresUnspannedFlex();
    TestGridRerunColumnSizingMultipleItems();
    TestShrinkToFitExcludesMargin();
    TestGridPercentRerunIndefiniteContainer();
    TestGridPercentRowsIndefiniteContainer();
    TestGridFitContentGrowthLimit();
    TestCompactLength();
    TestFitContentCalc();
    TestTaffyTreeBasics();
    TestTaffyTreeHierarchy();
    TestTaffyTreeStyleAndDirty();
    TestTaffyTreeMeasure();
    TestTaffyTreeLayout();
    TestHiddenLayout();
    TestFlexboxLayout();
    TestFlexNegativeMarginIntrinsic();
    TestFlexAspectRatioStretch();
    TestFlexMarginAfterBasisFloor();
    TestFlexColumnWrapIgnoresAncestorHeight();
    TestContentSizeIgnoresOwnHeightBasis();
    TestFlexBalanceCounts();
    TestFlexBalanceLayout();
    TestBlockLayout();
    TestFloatClearIntrinsicWidth();
    TestFloatPercentSumFits();
    TestFloatAvoidingAutoMargins();
    TestBlockJustifySelf();
    TestBlockPercentageBasis();
    TestBlockPercentPaddingUsesWidth();
    TestMarginCollapseThroughUsedHeight();
    TestRoundingCumulativePosition();
    TestBlockScrollBaselines();
    TestContainment();
    TestFlexCrossDefinitePercentage();
    TestFlexReverseBaseline();
    TestExplicitMinSkipsMinContent();
    TestAbsoluteKnownSizeSkipsMeasure();
    TestLeafContentSizeEndPadding();
    TestOverflowPastScrollOrigin();
    TestContentSizeEndPadding();
    TestScrollableOverflow();
    TestLayoutCache();

    // The grid tests build styles whose track lists live in an arena of their
    // own; a Style holds slices, not owned vectors.
    gGridArena = gpui::ArenaNew();
    TestNegativeAvailableSpace();
    TestGridBlockItemHeightCache();
    TestGridSparseSpanningLockedItems();
    TestGridIndefinitePercentMinTrack();
    TestGridCyclicPercentMinContribution();
    TestGridUnaffectedTracksNotMeasured();
    TestGridFrRowUsesColumnWidth();
    TestExplicitGridSizing();
    TestInitializeGridTracks();
    TestGridImplicitSizing();
    TestGridPlacement();
    TestGridLayout();
    TestGridFrContentProportion();
    TestGridAutoMarginBaseline();
    TestGridRepetitionLineNames();
    TestGridAreaEdges();
    TestGridNonFiniteStylesTerminate();
    TestGridZeroSpanTerminates();
    TestGridMinmaxAutoFixedZero();
    TestGridSizingKeywords();
    TestTaffy013Regressions();
    gpui::ArenaDelete(gGridArena);
    gGridArena = nullptr;
}
