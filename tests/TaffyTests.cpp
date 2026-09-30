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

static void TestStyleDefaults() {
    TestSuite("taffy::style (defaults_match)");

    taffy::Style s;
    utassert(s.display == taffy::Display::Flex);
    utassert(!s.itemIsTable);
    utassert(!s.itemIsReplaced);
    utassert(s.boxSizing == BoxSizing::BorderBox);
    utassert(s.direction == Direction::Ltr);
    utassert(s.overflow.x == taffy::Overflow::Visible);
    utassert(s.overflow.y == taffy::Overflow::Visible);
    utassertnear(s.scrollbarWidth, 0.0f);
    utassert(s.floatMode == Float::None);
    utassert(s.clear == Clear::None);
    utassert(s.position == taffy::Position::Relative);
    utassert(s.inset == RectLpa::Auto());
    utassert(s.margin == RectLpa::Zero());
    utassert(s.padding == RectLp::Zero());
    utassert(s.border == RectLp::Zero());
    utassert(s.size == SizeDim::Auto());
    utassert(s.minSize == SizeDim::Auto());
    utassert(s.maxSize == SizeDim::Auto());
    utassert(!IsSome(s.aspectRatio));
    utassert(!s.alignItems.IsSome());
    utassert(!s.alignSelf.IsSome());
    utassert(!s.justifyItems.IsSome());
    utassert(!s.justifySelf.IsSome());
    utassert(!s.alignContent.IsSome());
    utassert(!s.justifyContent.IsSome());
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
}

// ─── src/tree/taffy_tree.rs ──────────────────────────────────────────────

// Rust's `size_measure_function` from the same test module: the known
// dimensions if given, else the size stashed in the node context.
static SizeF SizeMeasureFunction(SizeFOpt knownDimensions,
                                 SizeAvail availableSpace, TaffyNodeId node,
                                 void* nodeContext, const taffy::Style* style,
                                 void* userData) {
    (void)availableSpace;
    (void)node;
    (void)style;
    (void)userData;
    SizeF ctx = nodeContext ? *(SizeF*)nodeContext : SizeF::Zero();
    return UnwrapOr(knownDimensions, ctx);
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
        rootStyle.justifyContent =
            OptJustifyContent(AlignContent{AlignContentKeyword::Center});
        rootStyle
            .alignItems = OptAlignItems(AlignItems{AlignItemsKeyword::Center});
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
}

// ─── src/compute/grid/explicit_grid.rs ───────────────────────────────────
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
        // explicit_grid_sizing_with_children
        LinePlacement cols[2] = {{GLine(1), GSpan(2)}, {GLine(-4), GAuto()}};
        LinePlacement rows[2] = {{GLine(2), GAuto()}, {GLine(-2), GAuto()}};
        uint16_t colCounts[3];
        uint16_t rowCounts[3];
        GridSizeEstimateForTest(6, 8, Direction::Ltr, cols, rows, 2, colCounts,
                                rowCounts);
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
        GridSizeEstimateForTest(4, 4, Direction::Ltr, cols, rows, 2, colCounts,
                                rowCounts);
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
    leaf.alignItems = taffy::OptAlignItems(
        taffy::AlignItems{taffy::AlignItemsKeyword::Center});
    leaf.justifyContent = taffy::OptJustifyContent(
        taffy::AlignContent{taffy::AlignContentKeyword::Center});
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
        l.contentSize = {120.0f, 95.0f};
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
    TestFlexDirection();
    TestStyleDefaults();
    TestCompactLength();
    TestTaffyTreeBasics();
    TestTaffyTreeHierarchy();
    TestTaffyTreeStyleAndDirty();
    TestTaffyTreeMeasure();
    TestTaffyTreeLayout();
    TestHiddenLayout();
    TestFlexboxLayout();
    TestBlockLayout();

    // The grid tests build styles whose track lists live in an arena of their
    // own; a Style holds slices, not owned vectors.
    gGridArena = gpui::ArenaNew();
    TestExplicitGridSizing();
    TestInitializeGridTracks();
    TestGridImplicitSizing();
    TestGridPlacement();
    TestGridLayout();
    TestTaffy013Regressions();
    gpui::ArenaDelete(gGridArena);
    gGridArena = nullptr;
}
