/* The flexbox layout algorithm — taffy/src/compute/flexbox.rs, which follows
 * https://www.w3.org/TR/css-flexbox-1/
 *
 * The step numbering in the comments is the spec's, kept from the Rust so the
 * two can be read side by side.
 */

#include "taffy/compute.h"

namespace taffy {
namespace {

// Rust's `Rect<bool>`, which flexbox only uses to remember which margins were
// `auto` before they were resolved to zero.
struct RectBool {
    bool left = false;
    bool right = false;
    bool top = false;
    bool bottom = false;

    bool MainStart(FlexDirection d) const { return IsRow(d) ? left : top; }
    bool MainEnd(FlexDirection d) const { return IsRow(d) ? right : bottom; }
    bool CrossStart(FlexDirection d) const { return IsRow(d) ? top : left; }
    bool CrossEnd(FlexDirection d) const { return IsRow(d) ? bottom : right; }
};

// The intermediate results of a flexbox calculation for a single item.
struct FlexItem {
    NodeId node;
    // The order of the node relative to its siblings.
    uint32_t order = 0;

    SizeFOpt size = SizeFOptNone();
    SizeFOpt minSize = SizeFOptNone();
    SizeFOpt maxSize = SizeFOptNone();
    Optf aspectRatio = None();
    AlignSelf alignSelf;

    PointOverflow overflow;
    float scrollbarWidth = 0.0f;
    float flexShrink = 0.0f;
    float flexGrow = 0.0f;

    // Differs from minSize above: this also accounts for content-based
    // automatic minimum sizes.
    float resolvedMinimumMainSize = 0.0f;

    RectFOpt inset = RectFOptNone();
    RectF margin;
    RectBool marginIsAuto;
    RectF padding;
    RectF border;

    // The default size of this item, and the same minus padding and border.
    float flexBasis = 0.0f;
    float innerFlexBasis = 0.0f;
    // How far this item has deviated from its target size.
    float violation = 0.0f;
    bool frozen = false;

    // Either the max- or min-content flex fraction.
    // https://www.w3.org/TR/css-flexbox-1/#intrinsic-main-sizes
    float contentFlexFraction = 0.0f;

    SizeF hypotheticalInnerSize;
    SizeF hypotheticalOuterSize;
    SizeF targetSize;
    SizeF outerTargetSize;

    // The position of the bottom edge of this item.
    float baseline = 0.0f;

    // The relative position from the item's natural flow position, from
    // relative position values, alignment and justification. Excludes
    // margin/padding/border.
    float offsetMain = 0.0f;
    float offsetCross = 0.0f;

    // https://www.w3.org/TR/css-overflow-3/#scroll-container
    bool IsScroll() const {
        return IsScrollContainer(overflow.x) || IsScrollContainer(overflow.y);
    }
};

// A line of FlexItems. `items` points into the container's item vector, which
// is not resized once the lines have been collected.
struct FlexLine {
    FlexItem* items = nullptr;
    int count = 0;
    float crossSize = 0.0f;
    float offsetCross = 0.0f;
};

// Values cached for the duration of the algorithm.
struct AlgoConstants {
    FlexDirection dir = FlexDirection::Row;
    Direction layoutDirection = Direction::Ltr;
    bool isRow = true;
    bool isColumn = false;
    bool isWrap = false;
    bool isWrapReverse = false;

    SizeFOpt minSize = SizeFOptNone();
    SizeFOpt maxSize = SizeFOptNone();
    RectF margin;
    RectF border;
    // padding + border + scrollbar gutter.
    RectF contentBoxInset;
    PointF scrollbarGutter;
    SizeF gap;
    AlignItems alignItems;
    AlignContent alignContent;
    OptJustifyContent justifyContent;

    // The border-box and content-box sizes of the node being laid out, if
    // known.
    SizeFOpt nodeOuterSize = SizeFOptNone();
    SizeFOpt nodeInnerSize = SizeFOptNone();

    // The size of the virtual container holding the flex items, and of the
    // internal container.
    SizeF containerSize;
    SizeF innerContainerSize;
};

// The total space gaps take up in an axis, given the gap size and how many
// items (children or flex lines) they sit between.
float SumAxisGaps(float gap, int numItems) {
    // Gaps only exist between items: fewer than two items means no gaps,
    // otherwise there are (numItems - 1) of them.
    if (numItems <= 1) {
        return 0.0f;
    }
    return gap * (float)(numItems - 1);
}

// Rust's `Option::filter`.
Optf Filter(Optf v, bool keep) {
    return keep ? v : None();
}

// ─── compute_constants ───────────────────────────────────────────────────

AlgoConstants ComputeConstants(TaffyTree* tree, const Style& style,
                               SizeFOpt knownDimensions, SizeFOpt parentSize) {
    CalcResolver calc = tree->calc;
    AlgoConstants c;
    c.dir = style.flexDirection;
    c.isRow = IsRow(c.dir);
    c.isColumn = IsColumn(c.dir);
    c.isWrap = style.flexWrap == FlexWrap::Wrap ||
               style.flexWrap == FlexWrap::WrapReverse;
    c.isWrapReverse = style.flexWrap == FlexWrap::WrapReverse;

    Optf aspectRatio = style.aspectRatio;
    c.margin = style.margin.ResolveOrZero(parentSize.w, calc);
    RectF padding = style.padding.ResolveOrZero(parentSize.w, calc);
    c.border = style.border.ResolveOrZero(parentSize.w, calc);
    SizeF paddingBorderSum = padding.SumAxes() + c.border.SumAxes();
    SizeF boxSizingAdjustment = style.boxSizing == BoxSizing::ContentBox
                                    ? paddingBorderSum
                                    : SizeF::Zero();

    c.alignItems = style.alignItems
                       .UnwrapOr(AlignItems{AlignItemsKeyword::Stretch});
    c.alignContent = style.alignContent
                         .UnwrapOr(AlignContent{AlignContentKeyword::Stretch});
    c.justifyContent = style.justifyContent;
    c.layoutDirection = style.direction;

    // A node that scrolls vertically needs *horizontal* space reserved for a
    // scrollbar, hence the transposed axes.
    PointOverflow t = style.overflow.Transpose();
    c.scrollbarGutter = {t.x == Overflow::Scroll ? style.scrollbarWidth : 0.0f,
                         t.y == Overflow::Scroll ? style.scrollbarWidth : 0.0f};
    c.contentBoxInset = padding + c.border;
    c.contentBoxInset.bottom += c.scrollbarGutter.y;
    if (c.layoutDirection == Direction::Ltr) {
        c.contentBoxInset.right += c.scrollbarGutter.x;
    } else {
        c.contentBoxInset.left += c.scrollbarGutter.x;
    }

    c.nodeOuterSize = knownDimensions;
    c.nodeInnerSize = MaybeSub(c.nodeOuterSize, c.contentBoxInset.SumAxes());
    c.gap = style.gap.ResolveOrZero(Or(c.nodeInnerSize, SizeFOpt{0, 0}), calc);

    c.minSize =
        MaybeAdd(MaybeApplyAspectRatio(
                     style.minSize.MaybeResolve(parentSize, calc), aspectRatio),
                 boxSizingAdjustment);
    c.maxSize =
        MaybeAdd(MaybeApplyAspectRatio(
                     style.maxSize.MaybeResolve(parentSize, calc), aspectRatio),
                 boxSizingAdjustment);
    return c;
}

// ─── generate_anonymous_flex_items ───────────────────────────────────────
//
// 9.1 Initial Setup — https://www.w3.org/TR/css-flexbox-1/#algo-anon-box

void GenerateAnonymousFlexItems(TaffyTree* tree, NodeId node,
                                const AlgoConstants& c,
                                Vec<FlexItem>* flexItems) {
    CalcResolver calc = tree->calc;
    int n = tree->ChildCount(node);
    // The child count is the upper bound on how many items come out of this
    // loop — absolute and display:none children are the only ones dropped —
    // so the whole list is one allocation. This is the hottest vec in the
    // tree: without it a flex container with four children reallocates and
    // memcpys a 192-byte `FlexItem` three times on the way to holding them,
    // once per container per layout pass. `bun cmd/vec-log.ts bench flexbox`
    // measured 17.0 MB of memcpy here, and none after.
    if (n > 0) {
        VecReserve(*flexItems, n);
    }
    for (int index = 0; index < n; index++) {
        NodeId child = tree->GetChildId(node, index);
        const Style& cs = tree->GetStyle(child);
        if (cs.position == Position::Absolute ||
            cs.BoxGenMode() == BoxGenerationMode::None) {
            continue;
        }

        Optf aspectRatio = cs.aspectRatio;
        RectF padding = cs.padding.ResolveOrZero(c.nodeInnerSize.w, calc);
        RectF border = cs.border.ResolveOrZero(c.nodeInnerSize.w, calc);
        SizeF pbSum = (padding + border).SumAxes();
        SizeF boxSizingAdjustment =
            cs.boxSizing == BoxSizing::ContentBox ? pbSum : SizeF::Zero();

        FlexItem item;
        item.node = child;
        item.order = (uint32_t)index;
        item.size = MaybeAdd(
            MaybeApplyAspectRatio(cs.size.MaybeResolve(c.nodeInnerSize, calc),
                                  aspectRatio),
            boxSizingAdjustment);
        item.minSize = MaybeAdd(cs.minSize.MaybeResolve(c.nodeInnerSize, calc),
                                boxSizingAdjustment);
        item.maxSize = MaybeAdd(cs.maxSize.MaybeResolve(c.nodeInnerSize, calc),
                                boxSizingAdjustment);
        item.aspectRatio = aspectRatio;
        // The inset resolves left/right against the width and top/bottom
        // against the height, which is Rust's `zip_size`.
        item.inset = cs.inset.MaybeResolveZip(c.nodeInnerSize, calc);
        item.margin = cs.margin.ResolveOrZero(c.nodeInnerSize.w, calc);
        item.marginIsAuto = {cs.margin.left.IsAuto(), cs.margin.right.IsAuto(),
                             cs.margin.top.IsAuto(), cs.margin.bottom.IsAuto()};
        item.padding = padding;
        item.border = border;
        item.alignSelf =
            ResolveSelfRelative(cs.alignSelf.UnwrapOr(c.alignItems),
                                cs.direction, c.layoutDirection, c.isColumn);
        item.overflow = cs.overflow;
        item.scrollbarWidth = cs.scrollbarWidth;
        item.flexGrow = cs.flexGrow;
        item.flexShrink = cs.flexShrink;
        VecAppend(*flexItems, item);
    }
}

// ─── determine_available_space ───────────────────────────────────────────
//
// 9.2 Line Length Determination —
// https://www.w3.org/TR/css-flexbox-1/#algo-available

SizeAvail DetermineAvailableSpace(SizeFOpt knownDimensions,
                                  SizeAvail outerAvailableSpace,
                                  const AlgoConstants& c) {
    // min/max/preferred size styles have already been folded into
    // knownDimensions by the caller.
    SizeAvail out;
    if (IsSome(knownDimensions.w)) {
        out.width = AvailableSpace::Definite(
            knownDimensions.w - c.contentBoxInset.HorizontalAxisSum());
    } else {
        out.width = MaybeSub(
            MaybeSub(outerAvailableSpace.width, c.margin.HorizontalAxisSum()),
            c.contentBoxInset.HorizontalAxisSum());
    }
    if (IsSome(knownDimensions.h)) {
        out.height = AvailableSpace::Definite(
            knownDimensions.h - c.contentBoxInset.VerticalAxisSum());
    } else {
        out.height = MaybeSub(
            MaybeSub(outerAvailableSpace.height, c.margin.VerticalAxisSum()),
            c.contentBoxInset.VerticalAxisSum());
    }
    return out;
}

// ─── determine_flex_base_size ────────────────────────────────────────────
//
// https://www.w3.org/TR/css-flexbox-1/#algo-main-item

void DetermineFlexBaseSize(TaffyTree* tree, const AlgoConstants& c,
                           SizeAvail availableSpace, FlexItem* items,
                           int count) {
    CalcResolver calc = tree->calc;
    FlexDirection dir = c.dir;

    for (int i = 0; i < count; i++) {
        FlexItem& child = items[i];
        const Style& cs = tree->GetStyle(child.node);

        Optf crossAxisParentSize = Cross(c.nodeInnerSize, dir);
        SizeFOpt childParentSize = SizeFOptFromCross(dir, crossAxisParentSize);

        float crossAxisMarginSum = CrossAxisSum(c.margin, dir);
        SizeFOpt transferredMinSize =
            MaybeApplyAspectRatio(child.minSize, child.aspectRatio);
        SizeFOpt transferredMaxSize =
            MaybeApplyAspectRatio(child.maxSize, child.aspectRatio);
        Optf childMinCross =
            MaybeAdd(Cross(transferredMinSize, dir), crossAxisMarginSum);
        Optf childMaxCross =
            MaybeAdd(Cross(transferredMaxSize, dir), crossAxisMarginSum);

        // Clamp the available space by the item's min- and max- size.
        AvailableSpace crossAxisAvailableSpace;
        AvailableSpace crossIn = availableSpace.Cross(dir);
        switch (crossIn.kind) {
            case AvailableSpace::Kind::Definite:
                crossAxisAvailableSpace = AvailableSpace::Definite(
                    MaybeClamp(UnwrapOr(crossAxisParentSize, crossIn.value),
                               childMinCross, childMaxCross));
                break;
            case AvailableSpace::Kind::MinContent:
                crossAxisAvailableSpace =
                    IsSome(childMinCross)
                        ? AvailableSpace::Definite(childMinCross)
                        : AvailableSpace::MinContent();
                break;
            default:
                crossAxisAvailableSpace =
                    IsSome(childMaxCross)
                        ? AvailableSpace::Definite(childMaxCross)
                        : AvailableSpace::MaxContent();
                break;
        }

        SizeFOpt childKnownDimensions = child.size;
        SetMain(&childKnownDimensions, dir, None());
        SetCross(&childKnownDimensions, dir,
                 MaybeClamp(Cross(childKnownDimensions, dir),
                            Cross(transferredMinSize, dir),
                            Cross(transferredMaxSize, dir)));
        if (child.alignSelf.keyword == AlignItemsKeyword::Stretch &&
            !child.marginIsAuto.CrossStart(dir) &&
            !child.marginIsAuto.CrossEnd(dir) &&
            !IsSome(Cross(childKnownDimensions, dir))) {
            SetCross(&childKnownDimensions, dir,
                     MaybeSub(crossAxisAvailableSpace.IntoOption(),
                              CrossAxisSum(child.margin, dir)));
        }

        Optf containerWidth = Main(c.nodeInnerSize, dir);
        float boxSizingAdjustment = 0.0f;
        if (cs.boxSizing == BoxSizing::ContentBox) {
            RectF padding = cs.padding.ResolveOrZero(containerWidth, calc);
            RectF border = cs.border.ResolveOrZero(containerWidth, calc);
            boxSizingAdjustment = Main((padding + border).SumAxes(), dir);
        }
        Optf flexBasis =
            MaybeAdd(cs.flexBasis.MaybeResolve(containerWidth, calc),
                     boxSizingAdjustment);

        // A. If the item has a definite used flex basis, that is the flex base
        //    size.
        // B. If the item has an intrinsic aspect ratio, a used flex basis of
        //    content and a definite cross size, the base size comes from its
        //    inner cross size and the ratio. `child.size` was already resolved
        //    against the aspect ratio in GenerateAnonymousFlexItems, so using
        //    the main size covers this.
        Optf mainSize = Main(child.size, dir);
        Optf definiteBasis = Or(flexBasis, mainSize);
        if (IsSome(definiteBasis)) {
            child.flexBasis = definiteBasis;
        } else {
            // C is covered by E below, which passes the available space
            // constraint through to the child.
            // D would need vertical writing modes.
            // E. Size the item into the available space using its used flex
            //    basis in place of its main size, treating `content` as
            //    max-content.
            SizeAvail childAvailableSpace = SizeAvail::MaxContent();
            childAvailableSpace
                .SetMain(dir, availableSpace.Main(dir).kind ==
                                      AvailableSpace::Kind::MinContent
                                  ? AvailableSpace::MinContent()
                                  : AvailableSpace::MaxContent());
            childAvailableSpace.SetCross(dir, crossAxisAvailableSpace);
            child.flexBasis = tree->MeasureChildSize(
                child.node, childKnownDimensions, childParentSize,
                childAvailableSpace, SizingMode::ContentSize, MainAxis(dir),
                LineBool::False());
        }

        // Floor the flex basis by the padding+border sum, which floors the
        // inner flex basis at zero. The spec says the content box should
        // *not* be floored here, but this matches Chrome and Firefox.
        float paddingBorderSum =
            MainAxisSum(child.padding, dir) + MainAxisSum(child.border, dir);
        child.flexBasis = F32Max(child.flexBasis, paddingBorderSum);

        child.innerFlexBasis = child.flexBasis -
                               MainAxisSum(child.padding, dir) -
                               MainAxisSum(child.border, dir);

        SizeFOpt paddingBorderAxesSums =
            AsOptional((child.padding + child.border).SumAxes());

        // The main-axis `parent_size` is deliberately left unset below: it is
        // used to resolve percentages, and a percentage size in an axis must
        // not contribute to a min-content contribution in that same axis. The
        // cross axis keeps its usual values so wrapping content wraps right.
        // https://drafts.csswg.org/css-sizing-3/#min-percentage-contribution
        SizeFOpt automaticMin = {MaybeIntoAutomaticMinSize(child.overflow.x),
                                 MaybeIntoAutomaticMinSize(child.overflow.y)};
        Optf styleMinMainSize = Main(Or(child.minSize, automaticMin), dir);

        if (IsSome(styleMinMainSize)) {
            child.resolvedMinimumMainSize = styleMinMainSize;
        } else {
            SizeAvail childAvailableSpace = SizeAvail::MinContent();
            childAvailableSpace.SetCross(dir, crossAxisAvailableSpace);
            float minContentMainSize = tree->MeasureChildSize(
                child.node, childKnownDimensions, childParentSize,
                childAvailableSpace, SizingMode::ContentSize, MainAxis(dir),
                LineBool::False());
            // 4.5 Automatic Minimum Size of Flex Items
            // https://www.w3.org/TR/css-flexbox-1/#min-size-auto
            float clamped =
                MaybeMin(MaybeMin(minContentMainSize, Main(child.size, dir)),
                         Main(transferredMaxSize, dir));
            child.resolvedMinimumMainSize =
                MaybeMax(clamped, Main(paddingBorderAxesSums, dir));
        }

        float hypotheticalInnerMinMain =
            MaybeMax(MaybeMax(child.resolvedMinimumMainSize,
                              Main(transferredMinSize, dir)),
                     Main(paddingBorderAxesSums, dir));
        float hypotheticalInnerSize =
            MaybeClamp(child.flexBasis, Some(hypotheticalInnerMinMain),
                       Main(transferredMaxSize, dir));
        float hypotheticalOuterSize =
            hypotheticalInnerSize + MainAxisSum(child.margin, dir);

        SetMain(&child.hypotheticalInnerSize, dir, hypotheticalInnerSize);
        SetMain(&child.hypotheticalOuterSize, dir, hypotheticalOuterSize);
    }
}

// ─── collect_flex_lines ──────────────────────────────────────────────────
//
// https://www.w3.org/TR/css-flexbox-1/#algo-line-break

void CollectFlexLines(const AlgoConstants& c, SizeAvail availableSpace,
                      Vec<FlexItem>* flexItems, Vec<FlexLine>* lines) {
    FlexItem* items = flexItems->els;
    int total = flexItems->len;

    if (!c.isWrap) {
        VecAppend(*lines, {items, total, 0.0f, 0.0f});
        return;
    }

    AvailableSpace mainAxisAvailableSpace;
    Optf maxMain = Main(c.maxSize, c.dir);
    if (IsSome(maxMain)) {
        mainAxisAvailableSpace = AvailableSpace::Definite(
            MaybeMax(UnwrapOr(availableSpace.Main(c.dir).IntoOption(), maxMain),
                     Main(c.minSize, c.dir)));
    } else {
        mainAxisAvailableSpace = availableSpace.Main(c.dir);
    }

    switch (mainAxisAvailableSpace.kind) {
        case AvailableSpace::Kind::MaxContent:
            // Under a max-content constraint the items never wrap.
            VecAppend(*lines, {items, total, 0.0f, 0.0f});
            return;
        case AvailableSpace::Kind::MinContent:
            // Under a min-content constraint every wrapping opportunity is
            // taken, so each item gets its own line.
            for (int i = 0; i < total; i++) {
                VecAppend(*lines, {items + i, 1, 0.0f, 0.0f});
            }
            return;
        default:
            break;
    }

    float limit = mainAxisAvailableSpace.value;
    float mainAxisGap = Main(c.gap, c.dir);
    int start = 0;
    while (start < total) {
        float lineLength = 0.0f;
        int index = total;
        for (int idx = start; idx < total; idx++) {
            // Gaps only occur between items, so the first one in a line does
            // not contribute one.
            float gapContribution = idx == start ? 0.0f : mainAxisGap;
            lineLength +=
                Main(items[idx].hypotheticalOuterSize, c.dir) + gapContribution;
            if (lineLength > limit && idx != start) {
                index = idx;
                break;
            }
        }
        VecAppend(*lines, {items + start, index - start, 0.0f, 0.0f});
        start = index;
    }
}

// The sum of the items' target sizes on a line, floored per item by its
// padding+border. Used twice by DetermineContainerMainSize.
float LineTotalTargetSize(const FlexLine& line, const AlgoConstants& c) {
    float total = 0.0f;
    for (int i = 0; i < line.count; i++) {
        const FlexItem& child = line.items[i];
        float paddingBorderSum =
            MainAxisSum(child.padding + child.border, c.dir);
        total += F32Max(MaybeMax(child.flexBasis, Main(child.minSize, c.dir)) +
                            MainAxisSum(child.margin, c.dir),
                        paddingBorderSum);
    }
    return total;
}

float LongestLineLength(Vec<FlexLine>* lines, const AlgoConstants& c) {
    float longest = 0.0f;
    for (int i = 0; i < lines->len; i++) {
        FlexLine& line = (*lines)[i];
        float lineGap = SumAxisGaps(Main(c.gap, c.dir), line.count);
        float total = LineTotalTargetSize(line, c) + lineGap;
        if (i == 0 || total > longest) {
            longest = total;
        }
    }
    return longest;
}

// ─── determine_container_main_size ───────────────────────────────────────

void DetermineContainerMainSize(TaffyTree* tree, SizeAvail availableSpace,
                                Vec<FlexLine>* lines, AlgoConstants* c) {
    FlexDirection dir = c->dir;
    float mainContentBoxInset = MainAxisSum(c->contentBoxInset, dir);

    float outerMainSize;
    Optf known = Main(c->nodeOuterSize, dir);
    if (IsSome(known)) {
        outerMainSize = known;
    } else {
        AvailableSpace mainAvail = availableSpace.Main(dir);
        if (mainAvail.kind == AvailableSpace::Kind::Definite) {
            float longest = LongestLineLength(lines, *c);
            float size = longest + mainContentBoxInset;
            outerMainSize =
                lines->len > 1 ? F32Max(size, mainAvail.value) : size;
        } else if (mainAvail.kind == AvailableSpace::Kind::MinContent &&
                   c->isWrap) {
            outerMainSize = LongestLineLength(lines, *c) + mainContentBoxInset;
        } else {
            // The flex container's max-content size is the largest sum of the
            // items' sizes within a single line.
            float mainSize = 0.0f;
            for (int li = 0; li < lines->len; li++) {
                FlexLine& line = (*lines)[li];
                for (int ii = 0; ii < line.count; ii++) {
                    FlexItem& item = line.items[ii];
                    Optf styleMin = Main(item.minSize, dir);
                    Optf stylePreferred = Main(item.size, dir);
                    Optf styleMax = Main(item.maxSize, dir);

                    // The spec reads as though `.maybe_max(style_preferred)`
                    // should not be here, but this matches Chrome and Firefox.
                    // https://www.w3.org/TR/css-flexbox-1/#change-2016-max-contribution
                    Optf clampingBasis =
                        MaybeMax(Some(item.flexBasis), stylePreferred);
                    Optf flexBasisMin =
                        Filter(clampingBasis, item.flexShrink == 0.0f);
                    Optf flexBasisMax =
                        Filter(clampingBasis, item.flexGrow == 0.0f);

                    float minMainSize = F32Max(
                        UnwrapOr(
                            Or(MaybeMax(styleMin, flexBasisMin), flexBasisMin),
                            item.resolvedMinimumMainSize),
                        item.resolvedMinimumMainSize);
                    float maxMainSize = UnwrapOr(
                        Or(MaybeMin(styleMax, flexBasisMax), flexBasisMax),
                        INFINITY);

                    float contentContribution;
                    if (IsSome(stylePreferred) &&
                        (maxMainSize <= minMainSize ||
                         maxMainSize <= stylePreferred)) {
                        // The clamping values override the content size, so
                        // computing it would be wasted work.
                        contentContribution =
                            F32Max(F32Min(stylePreferred, maxMainSize),
                                   minMainSize) +
                            MainAxisSum(item.margin, dir);
                    } else if (maxMainSize <= minMainSize) {
                        contentContribution =
                            minMainSize + MainAxisSum(item.margin, dir);
                    } else if (item.IsScroll()) {
                        contentContribution =
                            item.flexBasis + MainAxisSum(item.margin, dir);
                    } else {
                        Optf crossAxisParentSize = Cross(c->nodeInnerSize, dir);
                        float crossAxisMarginSum = CrossAxisSum(c->margin, dir);
                        Optf childMinCross = MaybeAdd(Cross(item.minSize, dir),
                                                      crossAxisMarginSum);
                        Optf childMaxCross = MaybeAdd(Cross(item.maxSize, dir),
                                                      crossAxisMarginSum);
                        AvailableSpace crossAxisAvailableSpace =
                            availableSpace.Cross(dir);
                        if (crossAxisAvailableSpace
                                .kind == AvailableSpace::Kind::Definite) {
                            crossAxisAvailableSpace = AvailableSpace::Definite(
                                UnwrapOr(crossAxisParentSize,
                                         crossAxisAvailableSpace.value));
                        }
                        crossAxisAvailableSpace =
                            MaybeClamp(crossAxisAvailableSpace, childMinCross,
                                       childMaxCross);

                        SizeAvail childAvailableSpace = availableSpace;
                        childAvailableSpace
                            .SetCross(dir, crossAxisAvailableSpace);

                        SizeFOpt childKnownDimensions = item.size;
                        SetMain(&childKnownDimensions, dir, None());
                        if (item.alignSelf
                                    .keyword == AlignItemsKeyword::Stretch &&
                            !IsSome(Cross(childKnownDimensions, dir))) {
                            SetCross(
                                &childKnownDimensions, dir,
                                MaybeSub(crossAxisAvailableSpace.IntoOption(),
                                         CrossAxisSum(item.margin, dir)));
                        }

                        float contentMainSize =
                            tree->MeasureChildSize(
                                item.node, childKnownDimensions,
                                c->nodeInnerSize, childAvailableSpace,
                                SizingMode::InherentSize, MainAxis(dir),
                                LineBool::False()) +
                            MainAxisSum(item.margin, dir);

                        // Asymmetrical between rows and columns. This likely
                        // relates to
                        // https://drafts.csswg.org/css-flexbox-1/#algo-main-container
                        // — "the automatic block size of a block-level flex
                        // container is its max-content size" — but it was
                        // found by matching Webkit/Firefox output rather than
                        // by reading the spec.
                        if (c->isRow) {
                            contentContribution =
                                MaybeClamp(contentMainSize, styleMin, styleMax);
                        } else {
                            contentContribution = MaybeClamp(
                                F32Max(contentMainSize, item.flexBasis),
                                styleMin, styleMax);
                        }
                    }

                    float diff = contentContribution - item.flexBasis;
                    if (diff > 0.0f) {
                        item.contentFlexFraction =
                            diff / F32Max(1.0f, item.flexGrow);
                    } else if (diff < 0.0f) {
                        float scaledShrinkFactor =
                            F32Max(1.0f, item.flexShrink * item.innerFlexBasis);
                        item.contentFlexFraction = diff / scaledShrinkFactor;
                    } else {
                        item.contentFlexFraction = 0.0f;
                    }
                }

                // The spec says to scale everything by the line's max flex
                // fraction, but neither Chrome nor Firefox implements that, so
                // neither do we — each item uses its own fraction.
                //
                // Add each item's flex base size to the product of its flex
                // grow factor (or scaled shrink factor, if the fraction was
                // negative) and that fraction.
                float itemMainSizeSum = 0.0f;
                for (int ii = 0; ii < line.count; ii++) {
                    FlexItem& item = line.items[ii];
                    float flexFraction = item.contentFlexFraction;
                    float flexContribution = 0.0f;
                    if (item.contentFlexFraction > 0.0f) {
                        flexContribution =
                            F32Max(1.0f, item.flexGrow) * flexFraction;
                    } else if (item.contentFlexFraction < 0.0f) {
                        float scaledShrinkFactor =
                            F32Max(1.0f, item.flexShrink) * item.innerFlexBasis;
                        flexContribution = scaledShrinkFactor * flexFraction;
                    }
                    float size = item.flexBasis + flexContribution;
                    SetMain(&item.outerTargetSize, dir, size);
                    SetMain(&item.targetSize, dir, size);
                    itemMainSizeSum += size;
                }

                float gapSum = SumAxisGaps(Main(c->gap, dir), line.count);
                mainSize = F32Max(mainSize, itemMainSizeSum + gapSum);
            }
            outerMainSize = mainSize + mainContentBoxInset;
        }
    }

    outerMainSize = F32Max(
        MaybeClamp(outerMainSize, Main(c->minSize, dir), Main(c->maxSize, dir)),
        mainContentBoxInset - Main(c->scrollbarGutter, dir));
    float innerMainSize = F32Max(outerMainSize - mainContentBoxInset, 0.0f);
    SetMain(&c->containerSize, dir, outerMainSize);
    SetMain(&c->innerContainerSize, dir, innerMainSize);
    SetMain(&c->nodeInnerSize, dir, Some(innerMainSize));
}

// ─── resolve_flexible_lengths ────────────────────────────────────────────
//
// 9.7 Resolving Flexible Lengths —
// https://www.w3.org/TR/css-flexbox-1/#resolve-flexible-lengths

void ResolveFlexibleLengths(FlexLine* line, const AlgoConstants& c) {
    FlexDirection dir = c.dir;
    float totalMainAxisGap = SumAxisGaps(Main(c.gap, dir), line->count);

    // 1. Determine the used flex factor.
    float totalHypotheticalOuterMainSize = 0.0f;
    for (int i = 0; i < line->count; i++) {
        totalHypotheticalOuterMainSize +=
            Main(line->items[i].hypotheticalOuterSize, dir);
    }
    float usedFlexFactor = totalMainAxisGap + totalHypotheticalOuterMainSize;
    float innerMain = UnwrapOr(Main(c.nodeInnerSize, dir), 0.0f);
    bool growing = usedFlexFactor < innerMain;
    bool shrinking = usedFlexFactor > innerMain;
    bool exactlySized = !growing && !shrinking;

    // 2. Size inflexible items: freeze, setting the target main size to the
    //    hypothetical main size.
    for (int i = 0; i < line->count; i++) {
        FlexItem& child = line->items[i];
        float innerTargetSize = Main(child.hypotheticalInnerSize, dir);
        SetMain(&child.targetSize, dir, innerTargetSize);

        if (exactlySized ||
            (child.flexGrow == 0.0f && child.flexShrink == 0.0f) ||
            (growing && child.flexBasis > innerTargetSize) ||
            (shrinking && child.flexBasis < innerTargetSize)) {
            child.frozen = true;
            SetMain(&child.outerTargetSize, dir,
                    innerTargetSize + MainAxisSum(child.margin, dir));
        }
    }

    if (exactlySized) {
        return;
    }

    // 3. Calculate the initial free space.
    float usedSpace = totalMainAxisGap;
    for (int i = 0; i < line->count; i++) {
        FlexItem& child = line->items[i];
        usedSpace += child.frozen
                         ? Main(child.outerTargetSize, dir)
                         : child.flexBasis + MainAxisSum(child.margin, dir);
    }
    float initialFreeSpace =
        UnwrapOr(MaybeSub(Main(c.nodeInnerSize, dir), usedSpace), 0.0f);

    // 4. Loop.
    while (true) {
        // a. If every item on the line is frozen, the free space has been
        //    distributed.
        bool allFrozen = true;
        for (int i = 0; i < line->count; i++) {
            if (!line->items[i].frozen) {
                allFrozen = false;
                break;
            }
        }
        if (allFrozen) {
            break;
        }

        // b. Recalculate the remaining free space. If the unfrozen items' flex
        //    factors sum to less than one, scale the initial free space by
        //    that sum and use it when it is the smaller magnitude.
        usedSpace = totalMainAxisGap;
        float sumFlexGrow = 0.0f;
        float sumFlexShrink = 0.0f;
        for (int i = 0; i < line->count; i++) {
            FlexItem& child = line->items[i];
            usedSpace += child.frozen
                             ? Main(child.outerTargetSize, dir)
                             : child.flexBasis + MainAxisSum(child.margin, dir);
            if (!child.frozen) {
                sumFlexGrow += child.flexGrow;
                sumFlexShrink += child.flexShrink;
            }
        }

        Optf remaining = MaybeSub(Main(c.nodeInnerSize, dir), usedSpace);
        float freeSpace;
        if (growing && sumFlexGrow < 1.0f) {
            freeSpace = MaybeMin(
                initialFreeSpace * sumFlexGrow - totalMainAxisGap, remaining);
        } else if (shrinking && sumFlexShrink < 1.0f) {
            freeSpace = MaybeMax(
                initialFreeSpace * sumFlexShrink - totalMainAxisGap, remaining);
        } else {
            freeSpace = UnwrapOr(remaining, usedFlexFactor - usedSpace);
        }

        // c. Distribute the free space proportionally to the flex factors.
        //    Rust guards this with `f32::is_normal`, which excludes zero,
        //    subnormals, infinities and NaN.
        bool isNormal = std::isnormal(freeSpace);
        if (isNormal) {
            if (growing && sumFlexGrow > 0.0f) {
                for (int i = 0; i < line->count; i++) {
                    FlexItem& child = line->items[i];
                    if (child.frozen) {
                        continue;
                    }
                    SetMain(&child.targetSize, dir,
                            child.flexBasis +
                                freeSpace * (child.flexGrow / sumFlexGrow));
                }
            } else if (shrinking && sumFlexShrink > 0.0f) {
                float sumScaledShrinkFactor = 0.0f;
                for (int i = 0; i < line->count; i++) {
                    FlexItem& child = line->items[i];
                    if (!child.frozen) {
                        sumScaledShrinkFactor +=
                            child.innerFlexBasis * child.flexShrink;
                    }
                }
                if (sumScaledShrinkFactor > 0.0f) {
                    for (int i = 0; i < line->count; i++) {
                        FlexItem& child = line->items[i];
                        if (child.frozen) {
                            continue;
                        }
                        float scaledShrinkFactor =
                            child.innerFlexBasis * child.flexShrink;
                        SetMain(&child.targetSize, dir,
                                child.flexBasis +
                                    freeSpace * (scaledShrinkFactor /
                                                 sumScaledShrinkFactor));
                    }
                }
            }
        }

        // d. Fix min/max violations: clamp each unfrozen item's target main
        //    size and floor its content box at zero.
        float totalViolation = 0.0f;
        for (int i = 0; i < line->count; i++) {
            FlexItem& child = line->items[i];
            if (child.frozen) {
                continue;
            }
            Optf resolvedMinMain = Some(child.resolvedMinimumMainSize);
            Optf maxMain = Main(child.maxSize, dir);
            float clamped = F32Max(MaybeClamp(Main(child.targetSize, dir),
                                              resolvedMinMain, maxMain),
                                   0.0f);
            child.violation = clamped - Main(child.targetSize, dir);
            SetMain(&child.targetSize, dir, clamped);
            SetMain(&child.outerTargetSize, dir,
                    clamped + MainAxisSum(child.margin, dir));
            totalViolation += child.violation;
        }

        // e. Freeze over-flexed items: all of them if the total violation is
        //    zero, the min-violating ones if it is positive, the
        //    max-violating ones if it is negative.
        for (int i = 0; i < line->count; i++) {
            FlexItem& child = line->items[i];
            if (child.frozen) {
                continue;
            }
            if (totalViolation > 0.0f) {
                child.frozen = child.violation > 0.0f;
            } else if (totalViolation < 0.0f) {
                child.frozen = child.violation < 0.0f;
            } else {
                child.frozen = true;
            }
        }
    }
}

// ─── determine_hypothetical_cross_size ───────────────────────────────────
//
// https://www.w3.org/TR/css-flexbox-1/#algo-cross-item

void DetermineHypotheticalCrossSize(TaffyTree* tree, FlexLine* line,
                                    const AlgoConstants& c,
                                    SizeAvail availableSpace) {
    FlexDirection dir = c.dir;
    for (int i = 0; i < line->count; i++) {
        FlexItem& child = line->items[i];
        float paddingBorderSum =
            CrossAxisSum(child.padding + child.border, dir);

        AvailableSpace childKnownMain =
            AvailableSpace::Definite(Main(c.containerSize, dir));

        Optf transferredMinCross =
            Cross(MaybeApplyAspectRatio(child.minSize, child.aspectRatio), dir);
        Optf transferredMaxCross =
            Cross(MaybeApplyAspectRatio(child.maxSize, child.aspectRatio), dir);

        Optf childCross =
            MaybeMax(MaybeClamp(Cross(child.size, dir), transferredMinCross,
                                transferredMaxCross),
                     paddingBorderSum);

        AvailableSpace childAvailableCross =
            MaybeMax(MaybeClamp(availableSpace.Cross(dir), transferredMinCross,
                                transferredMaxCross),
                     paddingBorderSum);

        float childInnerCross;
        if (IsSome(childCross)) {
            childInnerCross = childCross;
        } else {
            SizeFOpt known = {c.isRow ? Some(child.targetSize.w) : childCross,
                              c.isRow ? childCross : Some(child.targetSize.h)};
            SizeAvail avail = {c.isRow ? childKnownMain : childAvailableCross,
                               c.isRow ? childAvailableCross : childKnownMain};
            float measured = tree->MeasureChildSize(
                child.node, known, c.nodeInnerSize, avail,
                SizingMode::ContentSize, CrossAxis(dir), LineBool::False());
            childInnerCross = F32Max(
                MaybeClamp(measured, transferredMinCross, transferredMaxCross),
                paddingBorderSum);
        }
        float childOuterCross =
            childInnerCross + CrossAxisSum(child.margin, dir);

        SetCross(&child.hypotheticalInnerSize, dir, childInnerCross);
        SetCross(&child.hypotheticalOuterSize, dir, childOuterCross);
    }
}

// ─── calculate_children_base_lines ───────────────────────────────────────

void CalculateChildrenBaseLines(TaffyTree* tree, SizeFOpt nodeSize,
                                SizeAvail availableSpace, Vec<FlexLine>* lines,
                                const AlgoConstants& c) {
    // Baselines are only computed for flex rows: baseline alignment is only
    // supported in the cross axis where that axis is also the inline axis.
    if (!c.isRow) {
        return;
    }

    for (int li = 0; li < lines->len; li++) {
        FlexLine& line = (*lines)[li];
        // Baseline alignment is a no-op on a line with one or zero
        // participating items.
        int participating = 0;
        for (int i = 0; i < line.count; i++) {
            if (line.items[i]
                    .alignSelf.keyword == AlignItemsKeyword::Baseline) {
                participating++;
            }
        }
        if (participating <= 1) {
            continue;
        }

        for (int i = 0; i < line.count; i++) {
            FlexItem& child = line.items[i];
            if (child.alignSelf.keyword != AlignItemsKeyword::Baseline) {
                continue;
            }
            SizeFOpt known = {c.isRow ? Some(child.targetSize.w)
                                      : Some(child.hypotheticalInnerSize.w),
                              c.isRow ? Some(child.hypotheticalInnerSize.h)
                                      : Some(child.targetSize.h)};
            SizeAvail avail = {
                c.isRow ? AvailableSpace::Definite(c.containerSize.w)
                        : availableSpace.width.MaybeSet(nodeSize.w),
                c.isRow ? availableSpace.height.MaybeSet(nodeSize.h)
                        : AvailableSpace::Definite(c.containerSize.h)};
            LayoutOutput out = tree->PerformChildLayout(
                child.node, known, c.nodeInnerSize, avail,
                SizingMode::ContentSize, LineBool::False());
            float baseline = UnwrapOr(out.firstBaselines.y, out.size.h);
            if (IsScrollContainer(child.overflow.y)) {
                baseline = F32Max(0.0f, F32Min(baseline, out.size.h));
            }
            child.baseline = baseline + child.margin.top;
        }
    }
}

// ─── calculate_cross_size ────────────────────────────────────────────────
//
// https://www.w3.org/TR/css-flexbox-1/#algo-cross-line

void CalculateCrossSize(Vec<FlexLine>* lines, SizeFOpt nodeSize,
                        const AlgoConstants& c) {
    FlexDirection dir = c.dir;
    if (lines->len == 0) {
        return;
    }
    // A single-line container with a definite cross size gives its line the
    // container's inner cross size.
    if (!c.isWrap && IsSome(Cross(nodeSize, dir))) {
        float crossAxisPaddingBorder = CrossAxisSum(c.contentBoxInset, dir);
        (*lines)[0].crossSize = UnwrapOr(
            MaybeMax(
                MaybeSub(MaybeClamp(Cross(nodeSize, dir), Cross(c.minSize, dir),
                                    Cross(c.maxSize, dir)),
                         crossAxisPaddingBorder),
                0.0f),
            0.0f);
        return;
    }

    // Otherwise, for each line: take the largest of the baseline-aligned
    // items' summed distances and of the other items' outer hypothetical
    // cross sizes, and zero.
    for (int li = 0; li < lines->len; li++) {
        FlexLine& line = (*lines)[li];
        float maxBaseline = 0.0f;
        for (int i = 0; i < line.count; i++) {
            maxBaseline = F32Max(maxBaseline, line.items[i].baseline);
        }
        float crossSize = 0.0f;
        for (int i = 0; i < line.count; i++) {
            FlexItem& child = line.items[i];
            float v;
            if (child.alignSelf.keyword == AlignItemsKeyword::Baseline &&
                !child.marginIsAuto.CrossStart(dir) &&
                !child.marginIsAuto.CrossEnd(dir)) {
                v = maxBaseline - child.baseline +
                    Cross(child.hypotheticalOuterSize, dir);
            } else {
                v = Cross(child.hypotheticalOuterSize, dir);
            }
            crossSize = F32Max(crossSize, v);
        }
        line.crossSize = crossSize;
    }

    // A single-line container clamps its line's cross size to its own min and
    // max cross sizes.
    if (!c.isWrap) {
        float crossAxisPaddingBorder = CrossAxisSum(c.contentBoxInset, dir);
        (*lines)[0].crossSize =
            MaybeClamp((*lines)[0].crossSize,
                       MaybeSub(Cross(c.minSize, dir), crossAxisPaddingBorder),
                       MaybeSub(Cross(c.maxSize, dir), crossAxisPaddingBorder));
    }
}

// ─── handle_align_content_stretch ────────────────────────────────────────
//
// https://www.w3.org/TR/css-flexbox-1/#algo-line-stretch

void HandleAlignContentStretch(Vec<FlexLine>* lines, SizeFOpt nodeSize,
                               const AlgoConstants& c) {
    if (c.alignContent.keyword != AlignContentKeyword::Stretch ||
        lines->len == 0) {
        return;
    }
    FlexDirection dir = c.dir;
    float crossAxisPaddingBorder = CrossAxisSum(c.contentBoxInset, dir);
    Optf crossMinSize = Cross(c.minSize, dir);
    Optf crossMaxSize = Cross(c.maxSize, dir);
    float containerMinInnerCross = UnwrapOr(
        MaybeMax(MaybeSub(MaybeClamp(Or(Cross(nodeSize, dir), crossMinSize),
                                     crossMinSize, crossMaxSize),
                          crossAxisPaddingBorder),
                 0.0f),
        0.0f);

    float totalCrossAxisGap = SumAxisGaps(Cross(c.gap, dir), lines->len);
    float linesTotalCross = totalCrossAxisGap;
    for (int i = 0; i < lines->len; i++) {
        linesTotalCross += (*lines)[i].crossSize;
    }

    if (linesTotalCross < containerMinInnerCross) {
        float addition =
            (containerMinInnerCross - linesTotalCross) / (float)lines->len;
        for (int i = 0; i < lines->len; i++) {
            (*lines)[i].crossSize += addition;
        }
    }
}

// ─── determine_used_cross_size ───────────────────────────────────────────
//
// https://www.w3.org/TR/css-flexbox-1/#algo-stretch

void DetermineUsedCrossSize(TaffyTree* tree, Vec<FlexLine>* lines,
                            const AlgoConstants& c) {
    CalcResolver calc = tree->calc;
    FlexDirection dir = c.dir;
    for (int li = 0; li < lines->len; li++) {
        FlexLine& line = (*lines)[li];
        float lineCrossSize = line.crossSize;

        for (int i = 0; i < line.count; i++) {
            FlexItem& child = line.items[i];
            const Style& cs = tree->GetStyle(child.node);
            float used;
            if (child.alignSelf.keyword == AlignItemsKeyword::Stretch &&
                !child.marginIsAuto.CrossStart(dir) &&
                !child.marginIsAuto.CrossEnd(dir) &&
                cs.size.Cross(dir).IsAuto()) {
                // This use of max_size is an exception to the rule that
                // max_size transfers through the aspect ratio. Chrome and
                // Firefox agree, and it is a reasonable reading of the spec.
                RectF padding = cs.padding.ResolveOrZero(c.nodeInnerSize, calc);
                RectF border = cs.border.ResolveOrZero(c.nodeInnerSize, calc);
                SizeF pbSum = (padding + border).SumAxes();
                SizeF boxSizingAdjustment =
                    cs.boxSizing == BoxSizing::ContentBox ? pbSum
                                                          : SizeF::Zero();
                SizeFOpt maxSizeIgnoringAspectRatio =
                    MaybeAdd(cs.maxSize.MaybeResolve(c.nodeInnerSize, calc),
                             boxSizingAdjustment);
                used =
                    MaybeClamp(lineCrossSize - CrossAxisSum(child.margin, dir),
                               Cross(child.minSize, dir),
                               Cross(maxSizeIgnoringAspectRatio, dir));
            } else {
                used = Cross(child.hypotheticalInnerSize, dir);
            }
            SetCross(&child.targetSize, dir, used);
            SetCross(&child.outerTargetSize, dir,
                     used + CrossAxisSum(child.margin, dir));
        }
    }
}

// ─── distribute_remaining_free_space ─────────────────────────────────────
//
// https://www.w3.org/TR/css-flexbox-1/#algo-main-align

void DistributeRemainingFreeSpace(Vec<FlexLine>* lines,
                                  const AlgoConstants& c) {
    FlexDirection dir = c.dir;
    for (int li = 0; li < lines->len; li++) {
        FlexLine& line = (*lines)[li];
        float totalMainAxisGap = SumAxisGaps(Main(c.gap, dir), line.count);
        float usedSpace = totalMainAxisGap;
        for (int i = 0; i < line.count; i++) {
            usedSpace += Main(line.items[i].outerTargetSize, dir);
        }
        float freeSpace = Main(c.innerContainerSize, dir) - usedSpace;

        int numAutoMargins = 0;
        for (int i = 0; i < line.count; i++) {
            FlexItem& child = line.items[i];
            if (child.marginIsAuto.MainStart(dir)) {
                numAutoMargins++;
            }
            if (child.marginIsAuto.MainEnd(dir)) {
                numAutoMargins++;
            }
        }

        // 1. Positive free space and at least one auto main margin: split the
        //    free space equally among them.
        if (freeSpace > 0.0f && numAutoMargins > 0) {
            float margin = freeSpace / (float)numAutoMargins;
            for (int i = 0; i < line.count; i++) {
                FlexItem& child = line.items[i];
                if (child.marginIsAuto.MainStart(dir)) {
                    if (c.isRow) {
                        child.margin.left = margin;
                    } else {
                        child.margin.top = margin;
                    }
                }
                if (child.marginIsAuto.MainEnd(dir)) {
                    if (c.isRow) {
                        child.margin.right = margin;
                    } else {
                        child.margin.bottom = margin;
                    }
                }
            }
        }

        // 2. Align the items along the main axis per justify-content.
        int numItems = line.count;
        bool layoutReverse = IsReverse(dir);
        float gap = Main(c.gap, dir);
        JustifyContent rawMode =
            c.justifyContent
                .UnwrapOr(AlignContent{AlignContentKeyword::FlexStart});
        AlignContentKeyword mode =
            ApplyAlignmentFallback(freeSpace, numItems, rawMode);

        for (int i = 0; i < numItems; i++) {
            FlexItem& child =
                layoutReverse ? line.items[numItems - 1 - i] : line.items[i];
            child.offsetMain = ComputeAlignmentOffset(
                freeSpace, numItems, gap, mode, layoutReverse, i == 0);
        }
    }
}

// ─── align_flex_items_along_cross_axis ───────────────────────────────────
//
// https://www.w3.org/TR/css-flexbox-1/#algo-cross-align

float AlignFlexItemsAlongCrossAxis(const FlexItem& child, float freeSpace,
                                   float maxBaseline,
                                   float maxBaselineToBottomDistance,
                                   const AlgoConstants& c) {
    bool crossAxisShouldReverse =
        c.isColumn && c.layoutDirection == Direction::Rtl;

    // A `safe` align-self whose item would overflow the line falls back to
    // logical Start, per CSS Box Alignment 3 §4.3. Otherwise the safety field
    // is dropped and the switch below sees a bare keyword.
    AlignItemsKeyword keyword = (child.alignSelf.IsSafe() && freeSpace < 0.0f)
                                    ? AlignItemsKeyword::Start
                                    : child.alignSelf.keyword;

    switch (keyword) {
        case AlignItemsKeyword::Start:
            return crossAxisShouldReverse ? freeSpace : 0.0f;
        case AlignItemsKeyword::FlexStart:
            return (c.isWrapReverse != crossAxisShouldReverse) ? freeSpace
                                                               : 0.0f;
        case AlignItemsKeyword::End:
            return crossAxisShouldReverse ? 0.0f : freeSpace;
        case AlignItemsKeyword::FlexEnd:
            return (c.isWrapReverse != crossAxisShouldReverse) ? 0.0f
                                                               : freeSpace;
        case AlignItemsKeyword::Center:
            return freeSpace / 2.0f;
        case AlignItemsKeyword::Baseline: {
            if (c.isRow) {
                if (c.isWrapReverse) {
                    float lineCrossSize =
                        freeSpace + Cross(child.outerTargetSize, c.dir);
                    return lineCrossSize - maxBaselineToBottomDistance -
                           child.baseline;
                }
                return maxBaseline - child.baseline;
            }
            // Without vertical writing modes, baseline alignment only makes
            // sense in a row, so a column treats it as flex-start.
            bool baselineColumnShouldReverse =
                crossAxisShouldReverse && !c.isWrap;
            return (c.isWrapReverse != baselineColumnShouldReverse) ? freeSpace
                                                                    : 0.0f;
        }
        default: // Stretch
            return (c.isWrapReverse != crossAxisShouldReverse) ? freeSpace
                                                               : 0.0f;
    }
}

// ─── resolve_cross_axis_auto_margins ─────────────────────────────────────
//
// https://www.w3.org/TR/css-flexbox-1/#algo-cross-margins

void ResolveCrossAxisAutoMargins(Vec<FlexLine>* lines, const AlgoConstants& c) {
    FlexDirection dir = c.dir;
    for (int li = 0; li < lines->len; li++) {
        FlexLine& line = (*lines)[li];
        float lineCrossSize = line.crossSize;
        float maxBaseline = 0.0f;
        float maxBaselineToBottomDistance = 0.0f;
        for (int i = 0; i < line.count; i++) {
            maxBaseline = F32Max(maxBaseline, line.items[i].baseline);
            if (line.items[i]
                    .alignSelf.keyword == AlignItemsKeyword::Baseline) {
                maxBaselineToBottomDistance =
                    F32Max(maxBaselineToBottomDistance,
                           Cross(line.items[i].outerTargetSize, c.dir) -
                               line.items[i].baseline);
            }
        }

        for (int i = 0; i < line.count; i++) {
            FlexItem& child = line.items[i];
            float freeSpace = lineCrossSize - Cross(child.outerTargetSize, dir);

            if (child.marginIsAuto.CrossStart(dir) && child.marginIsAuto
                                                          .CrossEnd(dir)) {
                if (c.isRow) {
                    child.margin.top = freeSpace / 2.0f;
                    child.margin.bottom = freeSpace / 2.0f;
                } else {
                    child.margin.left = freeSpace / 2.0f;
                    child.margin.right = freeSpace / 2.0f;
                }
            } else if (child.marginIsAuto.CrossStart(dir)) {
                if (c.isRow) {
                    child.margin.top = freeSpace;
                } else {
                    child.margin.left = freeSpace;
                }
            } else if (child.marginIsAuto.CrossEnd(dir)) {
                if (c.isRow) {
                    child.margin.bottom = freeSpace;
                } else {
                    child.margin.right = freeSpace;
                }
            } else {
                // 14. Align all flex items along the cross axis.
                child.offsetCross = AlignFlexItemsAlongCrossAxis(
                    child, freeSpace, maxBaseline, maxBaselineToBottomDistance,
                    c);
            }
        }
    }
}

// ─── determine_container_cross_size ──────────────────────────────────────
//
// https://www.w3.org/TR/css-flexbox-1/#algo-cross-container

float DetermineContainerCrossSize(Vec<FlexLine>* lines, SizeFOpt nodeSize,
                                  AlgoConstants* c) {
    FlexDirection dir = c->dir;
    float totalCrossAxisGap = SumAxisGaps(Cross(c->gap, dir), lines->len);
    float totalLineCrossSize = 0.0f;
    for (int i = 0; i < lines->len; i++) {
        totalLineCrossSize += (*lines)[i].crossSize;
    }

    float paddingBorderSum = CrossAxisSum(c->contentBoxInset, dir);
    float crossScrollbarGutter = Cross(c->scrollbarGutter, dir);
    float outerContainerSize = F32Max(
        MaybeClamp(
            UnwrapOr(Cross(nodeSize, dir),
                     totalLineCrossSize + totalCrossAxisGap + paddingBorderSum),
            Cross(c->minSize, dir), Cross(c->maxSize, dir)),
        paddingBorderSum - crossScrollbarGutter);
    float innerContainerSize =
        F32Max(outerContainerSize - paddingBorderSum, 0.0f);

    SetCross(&c->containerSize, dir, outerContainerSize);
    SetCross(&c->innerContainerSize, dir, innerContainerSize);

    return totalLineCrossSize;
}

// ─── align_flex_lines_per_align_content ──────────────────────────────────
//
// https://www.w3.org/TR/css-flexbox-1/#algo-line-align

void AlignFlexLinesPerAlignContent(Vec<FlexLine>* lines, const AlgoConstants& c,
                                   float totalCrossSize) {
    int numLines = lines->len;
    float gap = Cross(c.gap, c.dir);
    float totalCrossAxisGap = SumAxisGaps(gap, numLines);
    float freeSpace =
        Cross(c.innerContainerSize, c.dir) - totalCrossSize - totalCrossAxisGap;

    AlignContentKeyword mode =
        ApplyAlignmentFallback(freeSpace, numLines, c.alignContent);

    for (int i = 0; i < numLines; i++) {
        FlexLine& line =
            c.isWrapReverse ? (*lines)[numLines - 1 - i] : (*lines)[i];
        line.offsetCross = ComputeAlignmentOffset(
            freeSpace, numLines, gap, mode, c.isWrapReverse, i == 0);
    }
}

// ─── calculate_flex_item ─────────────────────────────────────────────────

void CalculateFlexItem(TaffyTree* tree, FlexItem* item, float* totalOffsetMain,
                       float totalOffsetCross, float lineOffsetCross,
                       SizeF* totalContentSize, SizeF containerSize,
                       SizeFOpt nodeInnerSize, FlexDirection direction,
                       Direction layoutDirection) {
    LayoutOutput layoutOutput = tree->PerformChildLayout(
        item->node, AsOptional(item->targetSize), nodeInnerSize,
        SizeAvail::Definite(containerSize), SizingMode::ContentSize,
        LineBool::False());
    SizeF size = layoutOutput.size;
    SizeF contentSize = layoutOutput.contentSize;

    bool isRtlRow = IsRow(direction) && IsRtl(layoutDirection);
    bool isRtlColumn = IsColumn(direction) && IsRtl(layoutDirection);

    Optf negMainStart = MainStart(item->inset, direction);
    if (IsSome(negMainStart)) {
        negMainStart = -negMainStart;
    }
    Optf negMainEnd = MainEnd(item->inset, direction);
    if (IsSome(negMainEnd)) {
        negMainEnd = -negMainEnd;
    }
    float mainRelativeInset =
        isRtlRow
            ? UnwrapOr(Or(MainEnd(item->inset, direction), negMainStart), 0.0f)
            : UnwrapOr(Or(MainStart(item->inset, direction), negMainEnd), 0.0f);

    Optf negCrossEnd = CrossEnd(item->inset, direction);
    if (IsSome(negCrossEnd)) {
        negCrossEnd = -negCrossEnd;
    }
    float crossRelativeInset =
        isRtlColumn
            ? UnwrapOr(Or(negCrossEnd, CrossStart(item->inset, direction)),
                       0.0f)
            : UnwrapOr(Or(CrossStart(item->inset, direction), negCrossEnd),
                       0.0f);

    float effectiveLineOffsetCross = isRtlColumn ? 0.0f : lineOffsetCross;

    float offsetMain =
        isRtlRow
            ? *totalOffsetMain - item->offsetMain -
                  MainEnd(item->margin, direction) - mainRelativeInset - size.w
            : *totalOffsetMain + item->offsetMain +
                  MainStart(item->margin, direction) + mainRelativeInset;

    float offsetCross =
        totalOffsetCross + item->offsetCross + effectiveLineOffsetCross +
        CrossStart(item->margin, direction) + crossRelativeInset;

    float innerBaseline = UnwrapOr(layoutOutput.firstBaselines.y, size.h);
    if (IsRow(direction) && IsScrollContainer(item->overflow.y)) {
        innerBaseline = F32Max(0.0f, F32Min(innerBaseline, size.h));
    }
    if (IsRow(direction)) {
        float baselineOffsetCross = totalOffsetCross + item->offsetCross +
                                    effectiveLineOffsetCross +
                                    CrossStart(item->margin, direction);
        item->baseline = baselineOffsetCross + innerBaseline;
    } else {
        float baselineOffsetMain = *totalOffsetMain + item->offsetMain +
                                   MainStart(item->margin, direction);
        item->baseline = baselineOffsetMain + innerBaseline;
    }

    PointF location = IsRow(direction) ? PointF{offsetMain, offsetCross}
                                       : PointF{offsetCross, offsetMain};
    SizeF scrollbarSize = {
        item->overflow.y == Overflow::Scroll ? item->scrollbarWidth : 0.0f,
        item->overflow.x == Overflow::Scroll ? item->scrollbarWidth : 0.0f};

    Layout layout;
    layout.order = item->order;
    layout.size = size;
    layout.contentSize = contentSize;
    layout.scrollbarSize = scrollbarSize;
    layout.location = location;
    layout.padding = item->padding;
    layout.border = item->border;
    layout.margin = item->margin;
    tree->SetUnroundedLayout(item->node, layout);

    float advance = item->offsetMain + MainAxisSum(item->margin, direction) +
                    Main(size, direction);
    if (isRtlRow) {
        *totalOffsetMain -= advance;
    } else {
        *totalOffsetMain += advance;
    }

    PointF contributionLocation =
        IsRtl(layoutDirection)
            ? PointF{containerSize.w - (location.x + size.w), location.y}
            : location;
    *totalContentSize =
        Max(*totalContentSize,
            ComputeContentSizeContribution(contributionLocation, size,
                                           contentSize, item->overflow));
}

// ─── calculate_layout_line ───────────────────────────────────────────────

void CalculateLayoutLine(TaffyTree* tree, FlexLine* line,
                         float* totalOffsetCross, SizeF* contentSize,
                         SizeF containerSize, SizeFOpt nodeInnerSize,
                         RectF paddingBorder, FlexDirection direction,
                         Direction layoutDirection) {
    float totalOffsetMain =
        (IsRtl(layoutDirection) && IsRow(direction))
            ? containerSize.w - MainEnd(paddingBorder, direction)
            : MainStart(paddingBorder, direction);
    float lineOffsetCross = line->offsetCross;

    bool isRtlColumn = IsRtl(layoutDirection) && IsColumn(direction);
    if (isRtlColumn) {
        *totalOffsetCross -= lineOffsetCross + line->crossSize;
    }

    for (int i = 0; i < line->count; i++) {
        FlexItem* item = IsReverse(direction)
                             ? &line->items[line->count - 1 - i]
                             : &line->items[i];
        CalculateFlexItem(tree, item, &totalOffsetMain, *totalOffsetCross,
                          lineOffsetCross, contentSize, containerSize,
                          nodeInnerSize, direction, layoutDirection);
    }

    if (!isRtlColumn) {
        *totalOffsetCross += lineOffsetCross + line->crossSize;
    }
}

// ─── final_layout_pass ───────────────────────────────────────────────────

SizeF FinalLayoutPass(TaffyTree* tree, Vec<FlexLine>* lines,
                      const AlgoConstants& c) {
    float totalOffsetCross =
        (c.isColumn && IsRtl(c.layoutDirection))
            ? c.containerSize.w - CrossEnd(c.contentBoxInset, c.dir)
            : CrossStart(c.contentBoxInset, c.dir);

    SizeF contentSize = SizeF::Zero();

    for (int i = 0; i < lines->len; i++) {
        FlexLine& line =
            c.isWrapReverse ? (*lines)[lines->len - 1 - i] : (*lines)[i];
        CalculateLayoutLine(tree, &line, &totalOffsetCross, &contentSize,
                            c.containerSize, c.nodeInnerSize, c.contentBoxInset,
                            c.dir, c.layoutDirection);
    }

    contentSize.w +=
        IsRtl(c.layoutDirection)
            ? c.contentBoxInset.left - c.border.left - c.scrollbarGutter.x
            : c.contentBoxInset.right - c.border.right - c.scrollbarGutter.x;
    contentSize
        .h += c.contentBoxInset.bottom - c.border.bottom - c.scrollbarGutter.y;

    return contentSize;
}

// ─── perform_absolute_layout_on_absolute_children ────────────────────────

SizeF PerformAbsoluteLayoutOnAbsoluteChildren(TaffyTree* tree, NodeId node,
                                              const AlgoConstants& c) {
    CalcResolver calc = tree->calc;
    float containerWidth = c.containerSize.w;
    float containerHeight = c.containerSize.h;
    SizeF insetRelativeSize =
        c.containerSize - c.border.SumAxes() - IntoSize(c.scrollbarGutter);

    SizeF contentSize = SizeF::Zero();

    int n = tree->ChildCount(node);
    for (int order = 0; order < n; order++) {
        NodeId child = tree->GetChildId(node, order);
        const Style& cs = tree->GetStyle(child);

        if (cs.BoxGenMode() == BoxGenerationMode::None ||
            cs.position != Position::Absolute) {
            continue;
        }

        PointOverflow overflow = cs.overflow;
        float scrollbarWidth = cs.scrollbarWidth;
        Optf aspectRatio = cs.aspectRatio;
        AlignSelf alignSelf =
            ResolveSelfRelative(cs.alignSelf.UnwrapOr(c.alignItems),
                                cs.direction, c.layoutDirection, c.isColumn);
        RectFOpt margin = cs.margin
                              .MaybeResolve(Some(insetRelativeSize.w), calc);
        RectF padding = cs.padding
                            .ResolveOrZero(Some(insetRelativeSize.w), calc);
        RectF border = cs.border.ResolveOrZero(Some(insetRelativeSize.w), calc);
        SizeF paddingBorderSum = (padding + border).SumAxes();
        SizeF boxSizingAdjustment = cs.boxSizing == BoxSizing::ContentBox
                                        ? paddingBorderSum
                                        : SizeF::Zero();

        // Insets resolve against the container size minus its border.
        RectFOpt inset =
            cs.inset.MaybeResolveZip(AsOptional(insetRelativeSize), calc);
        Optf left = inset.left;
        Optf right = inset.right;
        Optf top = inset.top;
        Optf bottom = inset.bottom;

        SizeFOpt styleSize = MaybeAdd(
            MaybeApplyAspectRatio(
                cs.size.MaybeResolve(AsOptional(insetRelativeSize), calc),
                aspectRatio),
            boxSizingAdjustment);
        SizeFOpt minSize =
            MaybeMax(Or(MaybeAdd(MaybeApplyAspectRatio(
                                     cs.minSize.MaybeResolve(
                                         AsOptional(insetRelativeSize), calc),
                                     aspectRatio),
                                 boxSizingAdjustment),
                        AsOptional(paddingBorderSum)),
                     paddingBorderSum);
        SizeFOpt maxSize = MaybeAdd(
            MaybeApplyAspectRatio(
                cs.maxSize.MaybeResolve(AsOptional(insetRelativeSize), calc),
                aspectRatio),
            boxSizingAdjustment);
        SizeFOpt knownDimensions = MaybeClamp(styleSize, minSize, maxSize);

        // Fill in the width from left/right (and reapply the aspect ratio) if
        // the width is not known and both insets are set.
        if (!IsSome(knownDimensions.w) && IsSome(left) && IsSome(right)) {
            float newWidthRaw =
                MaybeSub(MaybeSub(insetRelativeSize.w, margin.left),
                         margin.right) -
                left - right;
            knownDimensions.w = Some(F32Max(newWidthRaw, 0.0f));
            knownDimensions =
                MaybeClamp(MaybeApplyAspectRatio(knownDimensions, aspectRatio),
                           minSize, maxSize);
        }
        if (!IsSome(knownDimensions.h) && IsSome(top) && IsSome(bottom)) {
            float newHeightRaw =
                MaybeSub(MaybeSub(insetRelativeSize.h, margin.top),
                         margin.bottom) -
                top - bottom;
            knownDimensions.h = Some(F32Max(newHeightRaw, 0.0f));
            knownDimensions =
                MaybeClamp(MaybeApplyAspectRatio(knownDimensions, aspectRatio),
                           minSize, maxSize);
        }

        SizeAvail childAvail = {AvailableSpace::Definite(MaybeClamp(
                                    containerWidth, minSize.w, maxSize.w)),
                                AvailableSpace::Definite(MaybeClamp(
                                    containerHeight, minSize.h, maxSize.h))};

        SizeF measuredSize = tree->MeasureChildSizeBoth(
            child, knownDimensions, c.nodeInnerSize, childAvail,
            SizingMode::InherentSize, LineBool::False());
        SizeF finalSize = MaybeClamp(UnwrapOr(knownDimensions, measuredSize),
                                     minSize, maxSize);

        LayoutOutput layoutOutput = tree->PerformChildLayout(
            child, AsOptional(finalSize), c.nodeInnerSize, childAvail,
            SizingMode::InherentSize, LineBool::False());

        RectF nonAutoMargin = {
            UnwrapOr(margin.left, 0.0f), UnwrapOr(margin.right, 0.0f),
            UnwrapOr(margin.top, 0.0f), UnwrapOr(margin.bottom, 0.0f)};

        SizeF freeSpace = Max(SizeF{c.containerSize.w - finalSize.w -
                                        nonAutoMargin.HorizontalAxisSum(),
                                    c.containerSize.h - finalSize.h -
                                        nonAutoMargin.VerticalAxisSum()},
                              SizeF::Zero());

        // Expand auto margins to fill the available space.
        int autoW =
            (IsSome(margin.left) ? 0 : 1) + (IsSome(margin.right) ? 0 : 1);
        int autoH =
            (IsSome(margin.top) ? 0 : 1) + (IsSome(margin.bottom) ? 0 : 1);
        SizeF autoMarginSize = {autoW > 0 && IsSome(left) && IsSome(right)
                                    ? freeSpace.w / (float)autoW
                                    : 0.0f,
                                autoH > 0 && IsSome(top) && IsSome(bottom)
                                    ? freeSpace.h / (float)autoH
                                    : 0.0f};
        RectF resolvedMargin = {UnwrapOr(margin.left, autoMarginSize.w),
                                UnwrapOr(margin.right, autoMarginSize.w),
                                UnwrapOr(margin.top, autoMarginSize.h),
                                UnwrapOr(margin.bottom, autoMarginSize.h)};

        // Flex-relative insets.
        Optf startMain = c.isRow ? left : top;
        Optf endMain = c.isRow ? right : bottom;
        Optf startCross = c.isRow ? top : left;
        Optf endCross = c.isRow ? bottom : right;
        bool mainIsRtl = c.isRow && IsRtl(c.layoutDirection);
        bool crossIsRtl = !c.isRow && IsRtl(c.layoutDirection);
        bool mainAxisFlexStartReversed = IsReverse(c.dir) != mainIsRtl;
        bool crossAxisFlexStartReversed = c.isWrapReverse != crossIsRtl;
        float mainStartScrollbarOffset =
            mainIsRtl ? Main(c.scrollbarGutter, c.dir) : 0.0f;
        float crossStartScrollbarOffset =
            crossIsRtl ? Cross(c.scrollbarGutter, c.dir) : 0.0f;
        float mainEndScrollbarOffset =
            mainIsRtl ? 0.0f : Main(c.scrollbarGutter, c.dir);
        float crossEndScrollbarOffset =
            crossIsRtl ? 0.0f : Cross(c.scrollbarGutter, c.dir);

        float alignedToMainEnd =
            Main(c.containerSize, c.dir) - MainEnd(c.border, c.dir) -
            mainEndScrollbarOffset - Main(finalSize, c.dir) -
            UnwrapOr(endMain, 0.0f) - MainEnd(resolvedMargin, c.dir);
        float offsetMain;
        if (IsSome(startMain) || IsSome(endMain)) {
            if (IsSome(startMain) && !(mainIsRtl && IsSome(endMain))) {
                offsetMain = startMain + MainStart(c.border, c.dir) +
                             mainStartScrollbarOffset +
                             MainStart(resolvedMargin, c.dir);
            } else {
                offsetMain = alignedToMainEnd;
            }
        } else {
            // Stretch is invalid for justify_content in flexbox, so it is
            // treated as unset (FlexStart).
            //
            // The `safe` overflow-position keyword is deliberately NOT applied
            // here even when the item overflows the main axis: Chrome does not
            // apply the safe fallback to justify-content on absolutely
            // positioned flex items (only cross-axis align-self does).
            float startPos = MainStart(c.contentBoxInset, c.dir) +
                             MainStart(resolvedMargin, c.dir);
            float endPos = Main(c.containerSize, c.dir) -
                           MainEnd(c.contentBoxInset, c.dir) -
                           Main(finalSize, c.dir) -
                           MainEnd(resolvedMargin, c.dir);
            AlignContentKeyword jc =
                c.justifyContent
                    .UnwrapOr(AlignContent{AlignContentKeyword::FlexStart})
                    .Keyword();
            bool rev = mainAxisFlexStartReversed;
            bool startPosition = jc == AlignContentKeyword::Start ? !mainIsRtl
                                 : jc == AlignContentKeyword::End ? mainIsRtl
                                                                  : true;
            switch (jc) {
                case AlignContentKeyword::SpaceBetween:
                case AlignContentKeyword::Stretch:
                case AlignContentKeyword::FlexStart:
                    offsetMain = rev ? endPos : startPos;
                    break;
                case AlignContentKeyword::FlexEnd:
                    offsetMain = rev ? startPos : endPos;
                    break;
                case AlignContentKeyword::Start:
                case AlignContentKeyword::End:
                    offsetMain = startPosition ? startPos : endPos;
                    break;
                default: // SpaceEvenly, SpaceAround, Center
                    offsetMain = (Main(c.containerSize, c.dir) +
                                  MainStart(c.contentBoxInset, c.dir) -
                                  MainEnd(c.contentBoxInset, c.dir) -
                                  Main(finalSize, c.dir) +
                                  MainStart(resolvedMargin, c.dir) -
                                  MainEnd(resolvedMargin, c.dir)) /
                                 2.0f;
                    break;
            }
        }

        float alignedToCrossEnd =
            Cross(c.containerSize, c.dir) - CrossEnd(c.border, c.dir) -
            crossEndScrollbarOffset - Cross(finalSize, c.dir) -
            UnwrapOr(endCross, 0.0f) - CrossEnd(resolvedMargin, c.dir);
        float offsetCross;
        if (IsSome(startCross) || IsSome(endCross)) {
            if (IsSome(startCross) && !(crossIsRtl && IsSome(endCross))) {
                offsetCross = startCross + CrossStart(c.border, c.dir) +
                              crossStartScrollbarOffset +
                              CrossStart(resolvedMargin, c.dir);
            } else {
                offsetCross = alignedToCrossEnd;
            }
        } else {
            bool crossOverflows =
                Cross(finalSize, c.dir) + CrossAxisSum(resolvedMargin, c.dir) >
                Cross(c.containerSize, c.dir) -
                    CrossAxisSum(c.contentBoxInset, c.dir);
            AlignItemsKeyword ck =
                ResolveSelfAlignmentSafety(alignSelf, crossOverflows);
            float startPos = CrossStart(c.contentBoxInset, c.dir) +
                             CrossStart(resolvedMargin, c.dir);
            float endPos = Cross(c.containerSize, c.dir) -
                           CrossEnd(c.contentBoxInset, c.dir) -
                           Cross(finalSize, c.dir) -
                           CrossEnd(resolvedMargin, c.dir);
            bool rev = crossAxisFlexStartReversed;
            bool startPosition = ck == AlignItemsKeyword::Start ||
                                         ck == AlignItemsKeyword::Baseline
                                     ? !crossIsRtl
                                 : ck == AlignItemsKeyword::End ? crossIsRtl
                                                                : true;
            switch (ck) {
                case AlignItemsKeyword::Start:
                case AlignItemsKeyword::End:
                case AlignItemsKeyword::Baseline:
                    offsetCross = startPosition ? startPos : endPos;
                    break;
                case AlignItemsKeyword::Center:
                    offsetCross = (Cross(c.containerSize, c.dir) +
                                   CrossStart(c.contentBoxInset, c.dir) -
                                   CrossEnd(c.contentBoxInset, c.dir) -
                                   Cross(finalSize, c.dir) +
                                   CrossStart(resolvedMargin, c.dir) -
                                   CrossEnd(resolvedMargin, c.dir)) /
                                  2.0f;
                    break;
                case AlignItemsKeyword::FlexEnd:
                    offsetCross = rev ? startPos : endPos;
                    break;
                default:
                    // Stretch and FlexStart. Stretch alignment does
                    // not apply to absolutely positioned items; see "Example
                    // 3" at https://www.w3.org/TR/css-flexbox-1/#abspos-items
                    offsetCross = rev ? endPos : startPos;
                    break;
            }
        }

        PointF location = c.isRow ? PointF{offsetMain, offsetCross}
                                  : PointF{offsetCross, offsetMain};
        SizeF scrollbarSize = {
            overflow.y == Overflow::Scroll ? scrollbarWidth : 0.0f,
            overflow.x == Overflow::Scroll ? scrollbarWidth : 0.0f};

        Layout layout;
        layout.order = (uint32_t)order;
        layout.size = finalSize;
        layout.contentSize = layoutOutput.contentSize;
        layout.scrollbarSize = scrollbarSize;
        layout.location = location;
        layout.padding = padding;
        layout.border = border;
        layout.margin = resolvedMargin;
        tree->SetUnroundedLayout(child, layout);

        SizeF sizeContribution = {
            overflow.x == Overflow::Visible
                ? F32Max(finalSize.w, layoutOutput.contentSize.w)
                : finalSize.w,
            overflow.y == Overflow::Visible
                ? F32Max(finalSize.h, layoutOutput.contentSize.h)
                : finalSize.h};
        if (HasNonZeroArea(sizeContribution)) {
            PointF absoluteAreaOffset = {
                c.border.left +
                    (IsRtl(c.layoutDirection) ? c.scrollbarGutter.x : 0.0f),
                c.border.top};
            PointF relativeLocation = {location.x - absoluteAreaOffset.x,
                                       location.y - absoluteAreaOffset.y};
            SizeF contribution;
            if (IsRtl(c.layoutDirection)) {
                float overflowExtraWidth =
                    F32Max(sizeContribution.w - finalSize.w, 0.0f);
                contribution.w =
                    F32Max(insetRelativeSize.w - relativeLocation.x, 0.0f) +
                    overflowExtraWidth;
            } else {
                contribution.w = relativeLocation.x + sizeContribution.w;
            }
            contribution.h = relativeLocation.y + sizeContribution.h;
            contentSize = Max(contentSize, contribution);
        }
    }

    return contentSize;
}

// ─── compute_preliminary ─────────────────────────────────────────────────

LayoutOutput ComputePreliminary(TaffyTree* tree, NodeId node,
                                const LayoutInput& inputs) {
    SizeFOpt knownDimensions = inputs.knownDimensions;
    SizeFOpt parentSize = inputs.parentSize;
    RunMode runMode = inputs.runMode;

    AlgoConstants constants = ComputeConstants(tree, tree->GetStyle(node),
                                               knownDimensions, parentSize);

    Vec<FlexItem> flexItems;
    // A container that does not wrap has exactly one flex line, and nothing
    // in this tree wraps into more: `bun cmd/vec-log.ts` over the taffy
    // suite, the story gallery, the showcase and the benchmarks finds no
    // container anywhere that takes a second one. So what this list costs is
    // one 24-byte allocation and free per container per layout pass, and
    // nothing else. Two lines of stack instead, and the third allocates.
    //
    // Two rather than more: the buffer sits on a frame that recurses once per
    // node, and a grid item is laid out through here too, so the frame's size
    // is not free. At four lines `grid/wide 100x100` was a repeatable 2%
    // slower; at two it is level, and the deep flexbox trees — where the
    // allocation was 3-5% of the run — keep all of it. Two is also the
    // largest that is never *worse* than allocating: an empty vec takes a
    // first block of four (`VecNextCap`), so a three- or four-line container
    // still allocates exactly once from here.
    FlexLine lineBuf[2];
    Vec<FlexLine> flexLines;
    VecUseExternalBuffer(flexLines, lineBuf);

    // 9.1. Initial Setup
    // 1. Generate anonymous flex items.
    GenerateAnonymousFlexItems(tree, node, constants, &flexItems);

    // 9.2. Line Length Determination
    // 2. Determine the available main and cross space for the flex items.
    SizeAvail availableSpace = DetermineAvailableSpace(
        knownDimensions, inputs.availableSpace, constants);

    // 3. Determine the flex base size and hypothetical main size of each item.
    DetermineFlexBaseSize(tree, constants, availableSpace, flexItems.els,
                          len(flexItems));

    // 4. Determine the main size of the flex container. Already done as part
    //    of ComputeConstants; the inner size is constants.nodeInnerSize.

    // 9.3. Main Size Determination
    // 5. Collect flex items into flex lines.
    CollectFlexLines(constants, availableSpace, &flexItems, &flexLines);

    // If the container size is undefined, determine the container's main size
    // and then re-resolve the gaps against it.
    Optf innerMainKnown = Main(constants.nodeInnerSize, constants.dir);
    if (IsSome(innerMainKnown)) {
        float outerMainSize =
            innerMainKnown +
            MainAxisSum(constants.contentBoxInset, constants.dir);
        SetMain(&constants.innerContainerSize, constants.dir, innerMainKnown);
        SetMain(&constants.containerSize, constants.dir, outerMainSize);
    } else {
        DetermineContainerMainSize(tree, availableSpace, &flexLines,
                                   &constants);
        SetMain(&constants.nodeInnerSize, constants.dir,
                Some(Main(constants.innerContainerSize, constants.dir)));
        SetMain(&constants.nodeOuterSize, constants.dir,
                Some(Main(constants.containerSize, constants.dir)));

        // Re-resolve percentage gaps against the size just determined.
        const Style& style = tree->GetStyle(node);
        float innerContainerSize =
            Main(constants.innerContainerSize, constants.dir);
        SizeF resolvedGap =
            style.gap.ResolveOrZero(Some(innerContainerSize), tree->calc);
        SetMain(&constants.gap, constants.dir,
                Main(resolvedGap, constants.dir));
    }

    // 6. Resolve the flexible lengths of all the flex items.
    for (int i = 0; i < len(flexLines); i++) {
        ResolveFlexibleLengths(&flexLines[i], constants);
    }

    // 9.4. Cross Size Determination
    // 7. Determine the hypothetical cross size of each item.
    for (int i = 0; i < len(flexLines); i++) {
        DetermineHypotheticalCrossSize(tree, &flexLines[i], constants,
                                       availableSpace);
    }

    // Child baselines, computed only where they are needed.
    CalculateChildrenBaseLines(tree, knownDimensions, availableSpace,
                               &flexLines, constants);

    // 8. Calculate the cross size of each flex line.
    CalculateCrossSize(&flexLines, knownDimensions, constants);

    // 9. Handle align-content: stretch.
    HandleAlignContentStretch(&flexLines, knownDimensions, constants);

    // 10. Collapse visibility:collapse items. Not implemented — taffy does not
    //     support visibility:collapse either.

    // 11. Determine the used cross size of each flex item.
    DetermineUsedCrossSize(tree, &flexLines, constants);

    // 9.5. Main-Axis Alignment
    // 12. Distribute any remaining free space.
    DistributeRemainingFreeSpace(&flexLines, constants);

    // 9.6. Cross-Axis Alignment
    // 13/14. Resolve cross-axis auto margins and align the items.
    ResolveCrossAxisAutoMargins(&flexLines, constants);

    // 15. Determine the flex container's used cross size.
    float totalLineCrossSize =
        DetermineContainerCrossSize(&flexLines, knownDimensions, &constants);

    // The container size is known; a caller that only wanted that is done.
    if (runMode == RunMode::ComputeSize) {
        VecReset(flexItems);
        VecReset(flexLines);
        return LayoutOutput::FromOuterSize(constants.containerSize);
    }

    // 16. Align all flex lines per align-content.
    AlignFlexLinesPerAlignContent(&flexLines, constants, totalLineCrossSize);

    SizeF inflowContentSize = FinalLayoutPass(tree, &flexLines, constants);

    SizeF absoluteContentSize =
        PerformAbsoluteLayoutOnAbsoluteChildren(tree, node, constants);

    // display:none children still get a zeroed layout.
    int nChildren = tree->ChildCount(node);
    for (int order = 0; order < nChildren; order++) {
        NodeId child = tree->GetChildId(node, order);
        if (tree->GetStyle(child).BoxGenMode() == BoxGenerationMode::None) {
            tree->SetUnroundedLayout(child, Layout::WithOrder((uint32_t)order));
            tree->PerformChildLayout(
                child, SizeFOptNone(), SizeFOptNone(), SizeAvail::MaxContent(),
                SizingMode::InherentSize, LineBool::False());
        }
    }

    // 8.5. Flex Container Baselines
    // https://www.w3.org/TR/css-flexbox-1/#flex-baselines
    Optf firstVerticalBaseline = None();
    int firstLineIdx = constants.isWrapReverse ? len(flexLines) - 1 : 0;
    if (firstLineIdx >= 0 && flexLines[firstLineIdx].count > 0) {
        FlexLine& firstLine = flexLines[firstLineIdx];
        const FlexItem* chosen = nullptr;
        for (int i = 0; i < firstLine.count; i++) {
            const FlexItem& item = firstLine.items[i];
            if (constants.isColumn ||
                item.alignSelf.keyword == AlignItemsKeyword::Baseline) {
                chosen = &item;
                break;
            }
        }
        if (!chosen) {
            chosen = &firstLine.items[0];
        }
        firstVerticalBaseline = Some(chosen->baseline);
    }

    VecReset(flexItems);
    VecReset(flexLines);

    return LayoutOutput::FromSizesAndBaselines(
        constants.containerSize, Max(inflowContentSize, absoluteContentSize),
        PointFOpt{None(), firstVerticalBaseline});
}

} // namespace

// ─── compute_flexbox_layout ──────────────────────────────────────────────

LayoutOutput ComputeFlexboxLayout(TaffyTree* tree, NodeId node,
                                  const LayoutInput& inputs) {
    CalcResolver calc = tree->calc;
    SizeFOpt knownDimensions = inputs.knownDimensions;
    SizeFOpt parentSize = inputs.parentSize;
    RunMode runMode = inputs.runMode;
    const Style& style = tree->GetStyle(node);

    Optf aspectRatio = style.aspectRatio;
    RectF padding = style.padding.ResolveOrZero(parentSize.w, calc);
    RectF border = style.border.ResolveOrZero(parentSize.w, calc);
    SizeF paddingBorderSum = padding.SumAxes() + border.SumAxes();
    SizeF boxSizingAdjustment = style.boxSizing == BoxSizing::ContentBox
                                    ? paddingBorderSum
                                    : SizeF::Zero();

    SizeFOpt minSize =
        MaybeAdd(MaybeApplyAspectRatio(
                     style.minSize.MaybeResolve(parentSize, calc), aspectRatio),
                 boxSizingAdjustment);
    SizeFOpt maxSize =
        MaybeAdd(MaybeApplyAspectRatio(
                     style.maxSize.MaybeResolve(parentSize, calc), aspectRatio),
                 boxSizingAdjustment);
    SizeFOpt clampedStyleSize = SizeFOptNone();
    if (inputs.sizingMode == SizingMode::InherentSize) {
        clampedStyleSize = MaybeClamp(
            MaybeAdd(
                MaybeApplyAspectRatio(style.size.MaybeResolve(parentSize, calc),
                                      aspectRatio),
                boxSizingAdjustment),
            minSize, maxSize);
    }

    // If both min and max are set in an axis and max <= min, that determines
    // the size in that axis.
    SizeFOpt minMaxDefiniteSize = SizeFOptNone();
    if (IsSome(minSize.w) && IsSome(maxSize.w) && maxSize.w <= minSize.w) {
        minMaxDefiniteSize.w = minSize.w;
    }
    if (IsSome(minSize.h) && IsSome(maxSize.h) && maxSize.h <= minSize.h) {
        minMaxDefiniteSize.h = minSize.h;
    }

    // The container's size is floored by its padding and border.
    SizeFOpt styledBasedKnownDimensions = Or(
        knownDimensions,
        MaybeMax(Or(minMaxDefiniteSize, clampedStyleSize), paddingBorderSum));

    // Short-circuit when the container's size is fully determined and only
    // the size was asked for.
    if (runMode == RunMode::ComputeSize) {
        if (BothAxisDefined(styledBasedKnownDimensions)) {
            return LayoutOutput::FromOuterSize(
                {styledBasedKnownDimensions.w, styledBasedKnownDimensions.h});
        }
        if (inputs.axis == RequestedAxis::Horizontal &&
            IsSome(styledBasedKnownDimensions.w)) {
            return LayoutOutput::FromOuterSize(
                {styledBasedKnownDimensions.w, 0.0f});
        }
    }

    LayoutInput next = inputs;
    next.knownDimensions = styledBasedKnownDimensions;
    return ComputePreliminary(tree, node, next);
}

} // namespace taffy
