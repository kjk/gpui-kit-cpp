/* Root, hidden, leaf and rounding passes plus the shared alignment and
 * content-size helpers — taffy/src/compute/mod.rs, compute/leaf.rs and
 * the two files of compute/common.
 */

#include "taffy/compute.h"

namespace taffy {

// ─── compute_root_layout ─────────────────────────────────────────────────

void ComputeRootLayout(TaffyTree* tree, NodeId root, SizeAvail availableSpace) {
    CalcResolver calc = tree->calc;
    Position rootPosition = tree->GetStyle(root).position;
    Direction rootDirection = tree->GetStyle(root).direction;
    SizeFOpt icbSize = availableSpace.IntoOptions();
    // An absolute or fixed root is positioned against the viewport. Without a
    // definite viewport in both axes there is no initial containing block, and
    // the root is laid out in flow instead.
    bool oofRoot =
        IsOutOfFlow(rootPosition) && IsSome(icbSize.w) && IsSome(icbSize.h);

    bool rootIsCached = false;
    Layout layout;
    OofCandidates candidates;

    if (oofRoot) {
        OofCandidate self;
        self.node = root;
        self.order = 0;
        self.position = rootPosition;
        float xAnchor = IsRtl(rootDirection) ? icbSize.w : 0.0f;
        self.staticX = AxisStaticPosition::FromEdge(
            xAnchor,
            IsRtl(rootDirection) ? AxisStaticEdge::End : AxisStaticEdge::Start);
        self.staticY =
            AxisStaticPosition::FromEdge(0.0f, AxisStaticEdge::Start);
        bool scroll = IsScrollContainer(tree->GetStyle(root).overflow.x) ||
                      IsScrollContainer(tree->GetStyle(root).overflow.y);
        OofBoxLayout box =
            LayoutOofBox(tree, self, {icbSize.w, icbSize.h}, {0.0f, 0.0f},
                         rootDirection, false, root, scroll, &rootIsCached);
        layout = box.layout;
        candidates = box.surfaced;
    } else {
        SizeFOpt knownDimensions = SizeFOptNone();

        {
            SizeFOpt parentSize = availableSpace.IntoOptions();
            const Style& style = tree->GetStyle(root);

            if (style.IsBlock()) {
                Optf aspectRatio = style.aspectRatio;
                RectF margin = style.margin.ResolveOrZero(parentSize.w, calc);
                RectF padding = style.padding.ResolveOrZero(parentSize.w, calc);
                RectF border = style.border.ResolveOrZero(parentSize.w, calc);
                SizeF paddingBorderSize = (padding + border).SumAxes();
                SizeF boxSizingAdjustment =
                    style.boxSizing == BoxSizing::ContentBox ? paddingBorderSize
                                                             : SizeF::Zero();

                SizeFOpt minSize =
                    MaybeAdd(MaybeApplyAspectRatio(
                                 style.minSize.MaybeResolve(parentSize, calc),
                                 aspectRatio),
                             boxSizingAdjustment);
                SizeFOpt maxSize =
                    MaybeAdd(MaybeApplyAspectRatio(
                                 style.maxSize.MaybeResolve(parentSize, calc),
                                 aspectRatio),
                             boxSizingAdjustment);
                SizeFOpt clampedStyleSize = MaybeClamp(
                    MaybeAdd(MaybeApplyAspectRatio(
                                 style.size.MaybeResolve(parentSize, calc),
                                 aspectRatio),
                             boxSizingAdjustment),
                    minSize, maxSize);

                // If both min and max are set in an axis and max <= min, that
                // determines the size in that axis.
                SizeFOpt minMaxDefiniteSize = SizeFOptNone();
                if (IsSome(minSize.w) && IsSome(maxSize.w) &&
                    maxSize.w <= minSize.w) {
                    minMaxDefiniteSize.w = minSize.w;
                }
                if (IsSome(minSize.h) && IsSome(maxSize.h) &&
                    maxSize.h <= minSize.h) {
                    minMaxDefiniteSize.h = minSize.h;
                }

                // Block nodes stretch their width to the available space when
                // it is definite.
                SizeFOpt availableSpaceBasedSize = SizeFOptNone();
                availableSpaceBasedSize
                    .w = MaybeSub(availableSpace.width.IntoOption(),
                                  margin.HorizontalAxisSum());

                SizeFOpt known = Or(knownDimensions, minMaxDefiniteSize);
                known = Or(known, clampedStyleSize);
                known = Or(known, availableSpaceBasedSize);
                knownDimensions = MaybeMax(known, paddingBorderSize);
            }
        }

        LayoutInput rootInput;
        rootInput.knownDimensions = knownDimensions;
        rootInput.parentSize = availableSpace.IntoOptions();
        rootInput.availableSpace = availableSpace;
        rootInput.sizingMode = SizingMode::InherentSize;
        rootInput.axis = RequestedAxis::Both;
        rootInput.runMode = RunMode::PerformLayout;
        rootInput.verticalMarginsAreCollapsible = LineBool::False();
        LayoutOutput cachedProbe;
        // A cached root already recorded its hoisted children, including the
        // ones this pass adds below. Laying the candidates out again is fine;
        // appending them again is not.
        rootIsCached = tree->CacheGet(root, rootInput, &cachedProbe);

        LayoutOutput output = tree->PerformChildLayout(
            root, knownDimensions, availableSpace.IntoOptions(), availableSpace,
            SizingMode::InherentSize, LineBool::False());

        const Style& style = tree->GetStyle(root);
        Optf widthOpt = availableSpace.width.IntoOption();
        RectF padding = style.padding.ResolveOrZero(widthOpt, calc);
        RectF border = style.border.ResolveOrZero(widthOpt, calc);
        RectF margin = style.margin.ResolveOrZero(widthOpt, calc);
        // A node that scrolls vertically needs horizontal space reserved, hence
        // the transposed axes.
        SizeF scrollbarSize = {
            style.overflow.y == Overflow::Scroll ? style.scrollbarWidth : 0.0f,
            style.overflow.x == Overflow::Scroll ? style.scrollbarWidth : 0.0f};
        // The root's margin box sits at the origin of the initial containing
        // block. RTL shifts it so the margin edge still meets the viewport.
        PointF location = {margin.left, margin.top};
        if (IsRtl(style.direction) && IsSome(widthOpt)) {
            location.x = widthOpt - output.size.w - margin.right;
        }
        // A relative root is shifted by its insets, resolved against the
        // viewport.
        if (style.position == Position::Relative) {
            RectFOpt inset = style.inset.MaybeResolveZip(icbSize, calc);
            Optf negRight = inset.right;
            if (IsSome(negRight)) {
                negRight = -negRight;
            }
            Optf negBottom = inset.bottom;
            if (IsSome(negBottom)) {
                negBottom = -negBottom;
            }
            Optf dx = IsRtl(style.direction) ? Or(negRight, inset.left)
                                             : Or(inset.left, negRight);
            location.x += UnwrapOr(dx, 0.0f);
            location.y += UnwrapOr(Or(inset.top, negBottom), 0.0f);
        }

        layout.order = 0;
        layout.location = location;
        layout.size = output.size;
        layout.scrollableOverflowRect = output.scrollableOverflowRect;
        layout.scrollbarSize = scrollbarSize;
        layout.padding = padding;
        layout.border = border;
        // TODO(taffy): support auto margins for the root node?
        layout.margin = margin;
        tree->SetUnroundedLayout(root, layout);
        candidates = output.oofCandidates;
    } // in-flow root

    // Boxes with no nearer containing block resolve against the viewport,
    // anchored at the canvas origin. An indefinite axis falls back to the
    // root's padding box. Overflow from this pass is not folded back into
    // the root layout.
    if (!candidates.IsEmpty()) {
        RectF areaInset = layout.border;
        areaInset.right += layout.scrollbarSize.w;
        areaInset.bottom += layout.scrollbarSize.h;
        SizeF rootPaddingBox = layout.size - areaInset.SumAxes();
        SizeF areaSize = rootPaddingBox;
        PointF areaOffset = {areaInset.left, areaInset.top};
        if (IsSome(icbSize.w)) {
            areaSize.w = F32Max(icbSize.w - layout.scrollbarSize.w, 0.0f);
            areaOffset.x = -layout.location.x;
        }
        if (IsSome(icbSize.h)) {
            areaSize.h = F32Max(icbSize.h - layout.scrollbarSize.h, 0.0f);
            areaOffset.y = -layout.location.y;
        }
        const Style& tailStyle = tree->GetStyle(root);
        Vec<NodeId> hoisted;
        OofCandidates unclaimed;
        RectF overflow = RectF::Zero();
        bool scroll = IsScrollContainer(tailStyle.overflow.x) ||
                      IsScrollContainer(tailStyle.overflow.y);
        PerformOofLayout(tree, root, candidates, areaSize, areaOffset,
                         rootDirection, ContainingBlockClaims::All(), scroll,
                         &hoisted, &unclaimed, &overflow);
        if (!rootIsCached) {
            tree->AddHoistedChildren(root, hoisted);
        }
    }
}

// ─── compute_hidden_layout ───────────────────────────────────────────────

LayoutOutput ComputeHiddenLayout(TaffyTree* tree, NodeId node) {
    tree->CacheClear(node);
    tree->SetUnroundedLayout(node, Layout::WithOrder(0));

    int n = tree->ChildCount(node);
    for (int i = 0; i < n; i++) {
        tree->ComputeChildLayout(tree->GetChildId(node, i),
                                 LayoutInput::Hidden());
    }
    return LayoutOutput::Hidden();
}

// ─── round_layout ────────────────────────────────────────────────────────
//
// Rounding is done on cumulative (viewport-relative) coordinates rather than
// parent-relative ones, and a width comes from rounding both edges and taking
// the difference rather than from rounding the width. Both keep gaps from
// opening up between siblings. See
// https://github.com/facebook/yoga/commit/aa5b296ac78f7a22e1aeaf4891243c6bb76488e2

static void RoundLayoutInner(TaffyTree* tree, NodeId nodeId, float cumulativeX,
                             float cumulativeY) {
    Layout unrounded = tree->GetUnroundedLayout(nodeId);
    Layout layout = unrounded;

    // A child of a fractionally placed parent is rounded in the same
    // cumulative space as its size, so siblings tile with no 1px gap.
    float parentX = cumulativeX;
    float parentY = cumulativeY;
    cumulativeX += unrounded.location.x;
    cumulativeY += unrounded.location.y;

    layout.location.x = F32Round(cumulativeX) - F32Round(parentX);
    layout.location.y = F32Round(cumulativeY) - F32Round(parentY);
    layout.size
        .w = F32Round(cumulativeX + unrounded.size.w) - F32Round(cumulativeX);
    layout.size
        .h = F32Round(cumulativeY + unrounded.size.h) - F32Round(cumulativeY);
    layout.scrollbarSize.w = F32Round(unrounded.scrollbarSize.w);
    layout.scrollbarSize.h = F32Round(unrounded.scrollbarSize.h);
    layout.border.left =
        F32Round(cumulativeX + unrounded.border.left) - F32Round(cumulativeX);
    layout.border.right =
        F32Round(cumulativeX + unrounded.size.w) -
        F32Round(cumulativeX + unrounded.size.w - unrounded.border.right);
    layout.border.top =
        F32Round(cumulativeY + unrounded.border.top) - F32Round(cumulativeY);
    layout.border.bottom =
        F32Round(cumulativeY + unrounded.size.h) -
        F32Round(cumulativeY + unrounded.size.h - unrounded.border.bottom);
    layout.padding.left =
        F32Round(cumulativeX + unrounded.padding.left) - F32Round(cumulativeX);
    layout.padding.right =
        F32Round(cumulativeX + unrounded.size.w) -
        F32Round(cumulativeX + unrounded.size.w - unrounded.padding.right);
    layout.padding.top =
        F32Round(cumulativeY + unrounded.padding.top) - F32Round(cumulativeY);
    layout.padding.bottom =
        F32Round(cumulativeY + unrounded.size.h) -
        F32Round(cumulativeY + unrounded.size.h - unrounded.padding.bottom);
    layout.scrollableOverflowRect
        .left = F32Round(cumulativeX + unrounded.scrollableOverflowRect.left) -
                F32Round(cumulativeX);
    layout.scrollableOverflowRect.right =
        F32Round(cumulativeX + unrounded.scrollableOverflowRect.right) -
        F32Round(cumulativeX);
    layout.scrollableOverflowRect
        .top = F32Round(cumulativeY + unrounded.scrollableOverflowRect.top) -
               F32Round(cumulativeY);
    layout.scrollableOverflowRect.bottom =
        F32Round(cumulativeY + unrounded.scrollableOverflowRect.bottom) -
        F32Round(cumulativeY);

    tree->SetFinalLayout(nodeId, layout);

    // Out-of-flow children are rounded from the containing block's hoisted
    // list, so their cumulative offset matches the block their location is
    // relative to. Walking them here as well would round them twice.
    int n = tree->ChildCount(nodeId);
    for (int i = 0; i < n; i++) {
        NodeId child = tree->GetChildId(nodeId, i);
        if (!IsOutOfFlow(tree->GetStyle(child).position)) {
            RoundLayoutInner(tree, child, cumulativeX, cumulativeY);
        }
    }
    int hoisted = tree->HoistedChildCount(nodeId);
    for (int i = 0; i < hoisted; i++) {
        RoundLayoutInner(tree, tree->HoistedChildAt(nodeId, i), cumulativeX,
                         cumulativeY);
    }
}

void RoundLayout(TaffyTree* tree, NodeId node) {
    RoundLayoutInner(tree, node, 0.0f, 0.0f);
}

// ─── compute_leaf_layout ─────────────────────────────────────────────────

LayoutOutput ComputeLeafLayout(const LayoutInput& inputs, const Style& style,
                               CalcResolver calc, LeafMeasureFn measure,
                               void* measureCtx) {
    SizeFOpt knownDimensions = inputs.knownDimensions;
    SizeFOpt parentSize = inputs.parentSize;
    SizeAvail availableSpaceIn = inputs.availableSpace;

    // Both horizontal and vertical percentage padding/border resolve against
    // the container's inline size (i.e. its width). That is CSS, not a bug.
    RectF margin = style.margin.ResolveOrZero(parentSize.w, calc);
    RectF padding = style.padding.ResolveOrZero(parentSize.w, calc);
    RectF border = style.border.ResolveOrZero(parentSize.w, calc);
    RectF paddingBorder = padding + border;
    SizeF pbSum = paddingBorder.SumAxes();
    SizeF boxSizingAdjustment =
        style.boxSizing == BoxSizing::ContentBox ? pbSum : SizeF::Zero();

    // In ContentSize mode the node's own size styles are ignored.
    SizeFOpt nodeSize = SizeFOptNone();
    SizeFOpt nodeMinSize = SizeFOptNone();
    SizeFOpt nodeMaxSize = SizeFOptNone();
    Optf aspectRatio = None();
    if (inputs.sizingMode == SizingMode::ContentSize) {
        nodeSize = knownDimensions;
    } else {
        aspectRatio = style.aspectRatio;
        SizeFOpt styleSize = MaybeAdd(
            MaybeApplyAspectRatio(style.size.MaybeResolve(parentSize, calc),
                                  aspectRatio),
            boxSizingAdjustment);
        SizeFOpt styleMinSize = MaybeAdd(
            MaybeApplyAspectRatio(style.minSize.MaybeResolve(parentSize, calc),
                                  aspectRatio),
            boxSizingAdjustment);
        SizeFOpt styleMaxSize = MaybeAdd(
            style.maxSize.MaybeResolve(parentSize, calc), boxSizingAdjustment);
        nodeSize = Or(knownDimensions, styleSize);
        nodeMinSize = styleMinSize;
        nodeMaxSize = styleMaxSize;
    }

    // A node that scrolls vertically needs *horizontal* space reserved for
    // its scrollbar, hence the transpose.
    PointOverflow t = style.overflow.Transpose();
    PointF scrollbarGutter = {
        t.x == Overflow::Scroll ? style.scrollbarWidth : 0.0f,
        t.y == Overflow::Scroll ? style.scrollbarWidth : 0.0f};
    // TODO(taffy): make the side configurable from the `direction` property.
    RectF contentBoxInset = paddingBorder;
    contentBoxInset.right += scrollbarGutter.x;
    contentBoxInset.bottom += scrollbarGutter.y;

    bool hasStylesPreventingBeingCollapsedThrough =
        !style.IsBlock() || IsScrollContainer(style.overflow.x) ||
        IsScrollContainer(style.overflow.y) || IsOutOfFlow(style.position) ||
        style.contain.EstablishesIndependentFormattingContext() ||
        padding.top > 0.0f || padding.bottom > 0.0f || border.top > 0.0f ||
        border.bottom > 0.0f || (IsSome(nodeSize.h) && nodeSize.h > 0.0f) ||
        (IsSome(nodeMinSize.h) && nodeMinSize.h > 0.0f);

    // Return early if both dimensions are already known.
    if (inputs.runMode == RunMode::ComputeSize &&
        hasStylesPreventingBeingCollapsedThrough && BothAxisDefined(nodeSize)) {
        SizeF size = {nodeSize.w, nodeSize.h};
        size = MaybeMax(MaybeClamp(size, nodeMinSize, nodeMaxSize),
                        AsOptional(pbSum));
        LayoutOutput out;
        out.size = size;
        return out;
    }

    SizeAvail availableSpace;
    AvailableSpace availWidth =
        IsSome(knownDimensions.w) ? AvailableSpace::Definite(knownDimensions.w)
                                  : availableSpaceIn.width;
    availableSpace.width = MaybeSub(availWidth, margin.HorizontalAxisSum())
                               .MaybeSet(knownDimensions.w)
                               .MaybeSet(nodeSize.w);
    if (availableSpace.width.IsDefinite()) {
        availableSpace.width =
            AvailableSpace::Definite(MaybeClamp(availableSpace.width.value,
                                                nodeMinSize.w, nodeMaxSize.w) -
                                     contentBoxInset.HorizontalAxisSum());
    }
    AvailableSpace availHeight =
        IsSome(knownDimensions.h) ? AvailableSpace::Definite(knownDimensions.h)
                                  : availableSpaceIn.height;
    availableSpace.height = MaybeSub(availHeight, margin.VerticalAxisSum())
                                .MaybeSet(knownDimensions.h)
                                .MaybeSet(nodeSize.h);
    if (availableSpace.height.IsDefinite()) {
        availableSpace.height =
            AvailableSpace::Definite(MaybeClamp(availableSpace.height.value,
                                                nodeMinSize.h, nodeMaxSize.h) -
                                     contentBoxInset.VerticalAxisSum());
    }

    // PerformHiddenLayout never reaches here; Rust spells that unreachable!().
    SizeFOpt measureKnown = inputs.runMode == RunMode::ComputeSize
                                ? knownDimensions
                                : SizeFOptNone();
    SizeF measuredSize = measure
                             ? measure(measureKnown, availableSpace, measureCtx)
                             : SizeF::Zero();

    SizeF clampedSize =
        MaybeClamp(UnwrapOr(Or(knownDimensions, nodeSize),
                            measuredSize + contentBoxInset.SumAxes()),
                   nodeMinSize, nodeMaxSize);
    SizeF size = {
        clampedSize.w,
        F32Max(clampedSize.h,
               IsSome(aspectRatio) ? clampedSize.w / aspectRatio : 0.0f)};
    size = MaybeMax(size, AsOptional(pbSum));

    LayoutOutput out;
    out.size = size;
    // End padding is part of a scroll container's overflow. Other boxes
    // do not extend content size by their own padding.
    bool isScrollContainer = IsScrollContainer(style.overflow.x) ||
                             IsScrollContainer(style.overflow.y);
    bool isRtl = IsRtl(style.direction);
    float startPadding = isRtl ? padding.right : padding.left;
    float endPadding = isRtl ? padding.left : padding.right;
    out.scrollableOverflowRect = {
        0.0f,
        startPadding + measuredSize.w + (isScrollContainer ? endPadding : 0.0f),
        0.0f,
        padding.top + measuredSize.h +
            (isScrollContainer ? padding.bottom : 0.0f)};
    out.marginsCanCollapseThrough = !hasStylesPreventingBeingCollapsedThrough &&
                                    size.h == 0.0f && measuredSize.h == 0.0f;
    return out;
}

// ─── shared alignment ────────────────────────────────────────────────────

AlignContentKeyword ApplyAlignmentFallback(float freeSpace, int numItems,
                                           AlignContent alignmentMode) {
    AlignContentKeyword keyword = alignmentMode.keyword;
    bool isSafe = alignmentMode.safety == AlignmentSafety::Safe;

    // 1. With a single item, or when the items overflow, the distributed
    //    keywords fall back to a positional one and gain implicit `safe`
    //    semantics so step 2 can flip them to Start.
    //    https://www.w3.org/TR/css-align-3/#distribution-values
    if (numItems <= 1 || freeSpace <= 0.0f) {
        switch (keyword) {
            case AlignContentKeyword::Stretch:
            case AlignContentKeyword::SpaceBetween:
                keyword = AlignContentKeyword::FlexStart;
                isSafe = true;
                break;
            case AlignContentKeyword::SpaceAround:
            case AlignContentKeyword::SpaceEvenly:
                keyword = AlignContentKeyword::Center;
                isSafe = true;
                break;
            default:
                break;
        }
    }

    // 2. Safe alignment falls back to Start whenever the subject would
    //    overflow the container.
    if (freeSpace <= 0.0f && isSafe) {
        keyword = AlignContentKeyword::Start;
    }
    return keyword;
}

float ComputeAlignmentOffset(float freeSpace, int numItems, float gap,
                             AlignContentKeyword alignmentMode,
                             bool layoutIsFlexReversed, bool isFirst) {
    if (isFirst) {
        switch (alignmentMode) {
            case AlignContentKeyword::Start:
                return 0.0f;
            case AlignContentKeyword::FlexStart:
                return layoutIsFlexReversed ? freeSpace : 0.0f;
            case AlignContentKeyword::End:
                return freeSpace;
            case AlignContentKeyword::FlexEnd:
                return layoutIsFlexReversed ? 0.0f : freeSpace;
            case AlignContentKeyword::Center:
                return freeSpace / 2.0f;
            case AlignContentKeyword::Stretch:
            case AlignContentKeyword::SpaceBetween:
                return 0.0f;
            case AlignContentKeyword::SpaceAround:
                return freeSpace >= 0.0f
                           ? (freeSpace /
                              (float)(numItems > 0 ? numItems : 1)) /
                                 2.0f
                           : freeSpace / 2.0f;
            case AlignContentKeyword::SpaceEvenly:
                return freeSpace >= 0.0f ? freeSpace / (float)(numItems + 1)
                                         : freeSpace / 2.0f;
        }
        return 0.0f;
    }

    float free = F32Max(freeSpace, 0.0f);
    switch (alignmentMode) {
        case AlignContentKeyword::SpaceBetween:
            return gap + free / (float)(numItems - 1);
        case AlignContentKeyword::SpaceAround:
            return gap + free / (float)numItems;
        case AlignContentKeyword::SpaceEvenly:
            return gap + free / (float)(numItems + 1);
        default:
            return gap;
    }
}

// ─── scrollable overflow ─────────────────────────────────────────────────

RectF ComputeScrollableOverflowContribution(PointF location, SizeF size,
                                            RectF scrollableOverflowRect,
                                            PointOverflow overflow,
                                            Contain contain,
                                            bool parentIsScrollContainer) {
    bool isScrollContainer =
        IsScrollContainer(overflow.x) || IsScrollContainer(overflow.y);
    // Layout or paint containment keeps overflowing content inside the box.
    bool overflowIsContained = contain.ContainsScrollableOverflow();
    // Hidden or Scroll on either axis clips both. Clip clips only its axis.
    // Visible is what lets a child's own overflow escape.
    bool propagatesX = !isScrollContainer && !overflowIsContained &&
                       overflow.x == Overflow::Visible;
    bool propagatesY = !isScrollContainer && !overflowIsContained &&
                       overflow.y == Overflow::Visible;
    SizeF endExtent = {
        propagatesX ? F32Max(size.w, scrollableOverflowRect.right) : size.w,
        propagatesY ? F32Max(size.h, scrollableOverflowRect.bottom) : size.h};
    if (endExtent.w <= 0.0f || endExtent.h <= 0.0f) {
        return RectF::Zero();
    }
    PointF startExtent = {
        propagatesX ? F32Min(0.0f, scrollableOverflowRect.left) : 0.0f,
        propagatesY ? F32Min(0.0f, scrollableOverflowRect.top) : 0.0f};
    RectF contribution = {location.x + startExtent.x, location.x + endExtent.w,
                          location.y + startExtent.y, location.y + endExtent.h};
    bool whollyUnreachable =
        contribution.right <= 0.0f || contribution.bottom <= 0.0f;
    if (parentIsScrollContainer && whollyUnreachable) {
        return RectF::Zero();
    }
    return contribution;
}

} // namespace taffy
