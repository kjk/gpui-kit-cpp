/* Out-of-flow positioning — taffy/src/compute/oof.rs.
 *
 * Block, flex and grid only collect candidates. This pass claims the ones
 * whose containing block is the current node and lays them out against that
 * node's positioning area. `position: fixed` is left for the root.
 */

#include "taffy/compute.h"
#include "taffy/sizing_keyword.h"

namespace taffy {

float ResolveStaticOffsetAxis(AxisStaticPosition position, float size,
                              float marginStart, float marginEnd) {
    bool overflows = position.align.safety == AlignmentSafety::Safe &&
                     size + marginStart + marginEnd > position.end - position
                                                                         .start;
    AxisStaticEdge keyword =
        overflows ? position.align.fallback : position.align.keyword;
    if (keyword == AxisStaticEdge::End) {
        return position.end - size - marginEnd;
    }
    if (keyword == AxisStaticEdge::Center) {
        return (position.start + position.end) / 2.0f - size / 2.0f +
               (marginStart - marginEnd) / 2.0f;
    }
    return position.start + marginStart;
}

static PointF ResolveStaticOffset(const OofCandidate& candidate, SizeF size,
                                  RectF margin) {
    return {ResolveStaticOffsetAxis(candidate.staticX, size.w, margin.left,
                                    margin.right),
            ResolveStaticOffsetAxis(candidate.staticY, size.h, margin.top,
                                    margin.bottom)};
}

void PerformOofLayout(TaffyTree* tree, NodeId nodeId, OofCandidates candidates,
                      SizeF areaSize, PointF areaOffset, Direction direction,
                      ContainingBlockClaims claims, bool isScrollContainer,
                      Vec<NodeId>* hoisted, OofCandidates* unclaimed,
                      RectF* overflowOut) {
    RectF absoluteOverflowRect = RectF::Zero();
    if (candidates.IsEmpty()) {
        *overflowOut = absoluteOverflowRect;
        return;
    }

    Vec<OofCandidate> work;
    for (int i = 0; i < len(candidates.inner); i++) {
        const OofCandidate& candidate = candidates.inner[i];
        if (claims.ForPosition(candidate.position)) {
            VecAppend(work, candidate);
        } else {
            unclaimed->Push(candidate);
        }
    }

    CalcResolver calc = tree->calc;
    int index = 0;
    while (index < len(work)) {
        OofCandidate candidate = work[index];
        index += 1;
        VecAppend(*hoisted, candidate.node);

        SizeF usedArea = areaSize;
        PointF usedOffset = areaOffset;
        RectF paddingBox = {areaOffset.x, areaOffset.x + areaSize.w,
                            areaOffset.y, areaOffset.y + areaSize.h};
        RectF gridArea;
        if (GridOofArea(tree, nodeId, candidate.node, paddingBox, &gridArea)) {
            usedArea = {gridArea.right - gridArea.left,
                        gridArea.bottom - gridArea.top};
            usedOffset = {gridArea.left, gridArea.top};
        }
        float areaWidth = usedArea.w;
        float areaHeight = usedArea.h;

        const Style& cs = tree->GetStyle(candidate.node);
        Optf aspectRatio = cs.aspectRatio;
        RectFOpt margin = cs.margin.MaybeResolve(Some(areaWidth), calc);
        RectF padding = cs.padding.ResolveOrZero(Some(areaWidth), calc);
        RectF border = cs.border.ResolveOrZero(Some(areaWidth), calc);
        SizeF paddingBorderSum = (padding + border).SumAxes();
        SizeF boxSizingAdjustment = cs.boxSizing == BoxSizing::ContentBox
                                        ? paddingBorderSum
                                        : SizeF::Zero();
        RectFOpt inset = cs.inset.MaybeResolveZip(AsOptional(usedArea), calc);
        Optf left = inset.left;
        Optf right = inset.right;
        Optf top = inset.top;
        Optf bottom = inset.bottom;
        SizeDim sizeStyle = cs.size;
        PointOverflow overflow = cs.overflow;
        Contain contain = cs.contain;
        float scrollbarWidth = cs.scrollbarWidth;

        SizeFOpt styleSize =
            MaybeAdd(MaybeApplyAspectRatio(
                         sizeStyle.MaybeResolve(AsOptional(usedArea), calc),
                         aspectRatio),
                     boxSizingAdjustment);
        SizeFOpt minSize = MaybeMax(
            Or(MaybeAdd(MaybeApplyAspectRatio(
                            cs.minSize.MaybeResolve(AsOptional(usedArea), calc),
                            aspectRatio),
                        boxSizingAdjustment),
               AsOptional(paddingBorderSum)),
            paddingBorderSum);
        SizeFOpt maxSize =
            MaybeAdd(MaybeApplyAspectRatio(
                         cs.maxSize.MaybeResolve(AsOptional(usedArea), calc),
                         aspectRatio),
                     boxSizingAdjustment);
        SizeFOpt knownDimensions = MaybeClamp(styleSize, minSize, maxSize);

        if (sizeStyle.width.IsSizingKeyword() || sizeStyle.height
                                                     .IsSizingKeyword()) {
            ResolveAbsoluteSizingKeywords(
                tree, candidate.node, &knownDimensions, sizeStyle, usedArea,
                inset, margin, SizingMode::ContentSize);
            knownDimensions =
                MaybeClamp(MaybeApplyAspectRatio(knownDimensions, aspectRatio),
                           minSize, maxSize);
        }

        if (!IsSome(knownDimensions.w) && IsSome(left) && IsSome(right)) {
            float newWidthRaw =
                MaybeSub(MaybeSub(areaWidth, margin.left), margin.right) -
                left - right;
            knownDimensions.w = Some(F32Max(newWidthRaw, 0.0f));
            knownDimensions =
                MaybeClamp(MaybeApplyAspectRatio(knownDimensions, aspectRatio),
                           minSize, maxSize);
        }
        if (!IsSome(knownDimensions.h) && IsSome(top) && IsSome(bottom)) {
            float newHeightRaw =
                MaybeSub(MaybeSub(areaHeight, margin.top), margin.bottom) -
                top - bottom;
            knownDimensions.h = Some(F32Max(newHeightRaw, 0.0f));
            knownDimensions =
                MaybeClamp(MaybeApplyAspectRatio(knownDimensions, aspectRatio),
                           minSize, maxSize);
        }

        SizeAvail childAvail = {AvailableSpace::Definite(MaybeClamp(
                                    areaWidth, minSize.w, maxSize.w)),
                                AvailableSpace::Definite(MaybeClamp(
                                    areaHeight, minSize.h, maxSize.h))};

        SizeF finalSize;
        if (IsSome(knownDimensions.w) && IsSome(knownDimensions.h)) {
            finalSize = {knownDimensions.w, knownDimensions.h};
        } else {
            SizeF measured = tree->MeasureChildSizeBoth(
                candidate.node, knownDimensions, AsOptional(usedArea),
                childAvail, SizingMode::ContentSize, LineBool::False());
            finalSize = UnwrapOr(knownDimensions, measured);
        }
        finalSize = MaybeClamp(finalSize, minSize, maxSize);

        LayoutOutput layoutOutput = tree->PerformChildLayout(
            candidate.node, AsOptional(finalSize), AsOptional(usedArea),
            childAvail, SizingMode::ContentSize, LineBool::False());

        RectF nonAutoMargin = {
            IsSome(left) ? UnwrapOr(margin.left, 0.0f) : 0.0f,
            IsSome(right) ? UnwrapOr(margin.right, 0.0f) : 0.0f,
            IsSome(top) ? UnwrapOr(margin.top, 0.0f) : 0.0f,
            IsSome(bottom) ? UnwrapOr(margin.bottom, 0.0f) : 0.0f};

        // Auto margins absorb free space only when both insets on that axis
        // are set. A single inset leaves them at 0.
        SizeF freeSpace = {0.0f, 0.0f};
        if (IsSome(left) && IsSome(right)) {
            freeSpace.w = usedArea.w - left - right - finalSize.w -
                          nonAutoMargin.HorizontalAxisSum();
        }
        if (IsSome(top) && IsSome(bottom)) {
            freeSpace.h = usedArea.h - top - bottom - finalSize.h -
                          nonAutoMargin.VerticalAxisSum();
        }
        int autoW =
            (IsSome(margin.left) ? 0 : 1) + (IsSome(margin.right) ? 0 : 1);
        int autoH =
            (IsSome(margin.top) ? 0 : 1) + (IsSome(margin.bottom) ? 0 : 1);
        SizeF autoMarginSize;
        if (autoW == 2 && freeSpace.w <= 0.0f) {
            autoMarginSize.w = 0.0f;
        } else if (autoW > 0) {
            autoMarginSize.w = freeSpace.w / (float)autoW;
        }
        if (autoH == 2 && freeSpace.h <= 0.0f) {
            autoMarginSize.h = 0.0f;
        } else if (autoH > 0) {
            autoMarginSize.h = freeSpace.h / (float)autoH;
        }
        RectF autoMargin = {IsSome(margin.left) ? 0.0f : autoMarginSize.w,
                            IsSome(margin.right) ? 0.0f : autoMarginSize.w,
                            IsSome(margin.top) ? 0.0f : autoMarginSize.h,
                            IsSome(margin.bottom) ? 0.0f : autoMarginSize.h};
        RectF resolvedMargin = {UnwrapOr(margin.left, autoMargin.left),
                                UnwrapOr(margin.right, autoMargin.right),
                                UnwrapOr(margin.top, autoMargin.top),
                                UnwrapOr(margin.bottom, autoMargin.bottom)};

        PointF staticOffset =
            ResolveStaticOffset(candidate, finalSize, resolvedMargin);
        float xOffset;
        if (IsSome(left) && IsSome(right)) {
            xOffset = IsRtl(direction) ? usedArea.w - finalSize.w - right -
                                             resolvedMargin.right
                                       : left + resolvedMargin.left;
        } else if (IsSome(left)) {
            xOffset = left + resolvedMargin.left;
        } else if (IsSome(right)) {
            xOffset = usedArea.w - finalSize.w - right - resolvedMargin.right;
        } else {
            xOffset = staticOffset.x - usedOffset.x;
        }
        float yLocation;
        if (IsSome(top)) {
            yLocation = top + resolvedMargin.top + usedOffset.y;
        } else if (IsSome(bottom)) {
            yLocation = usedArea.h - finalSize.h - bottom -
                        resolvedMargin.bottom + usedOffset.y;
        } else {
            yLocation = staticOffset.y;
        }
        PointF location = {xOffset + usedOffset.x, yLocation};

        SizeF scrollbarSize = {
            overflow.y == Overflow::Scroll ? scrollbarWidth : 0.0f,
            overflow.x == Overflow::Scroll ? scrollbarWidth : 0.0f};

        Layout layout;
        layout.order = candidate.order;
        layout.size = finalSize;
        layout.scrollableOverflowRect = layoutOutput.scrollableOverflowRect;
        layout.scrollbarSize = scrollbarSize;
        layout.location = location;
        layout.padding = padding;
        layout.border = border;
        layout.margin = resolvedMargin;
        tree->SetUnroundedLayout(candidate.node, layout);

        if (!layoutOutput.oofCandidates.IsEmpty()) {
            layoutOutput.oofCandidates.Translate(location);
            for (int s = 0; s < len(layoutOutput.oofCandidates.inner); s++) {
                const OofCandidate& surfaced = layoutOutput.oofCandidates
                                                   .inner[s];
                if (claims.ForPosition(surfaced.position)) {
                    VecAppend(work, surfaced);
                } else {
                    unclaimed->Push(surfaced);
                }
            }
        }

        float relX = location.x - usedOffset.x;
        PointF relativeLocation = {
            IsRtl(direction) ? usedArea.w - relX - finalSize.w : relX,
            location.y - usedOffset.y};
        absoluteOverflowRect = absoluteOverflowRect
                                   .Union(ComputeScrollableOverflowContribution(
                                       relativeLocation, finalSize,
                                       layoutOutput.scrollableOverflowRect,
                                       overflow, contain, isScrollContainer));
    }

    *overflowOut = absoluteOverflowRect;
}

void ComputeOofLayout(TaffyTree* tree, NodeId node, LayoutOutput* output) {
    if (!output->hasOofPositioningArea) {
        return;
    }
    const Style& style = tree->GetStyle(node);
    Direction direction = style.direction;
    ContainingBlockClaims claims = ContainingBlockFor(style.position);
    bool isScrollContainer = IsScrollContainer(style.overflow.x) ||
                             IsScrollContainer(style.overflow.y);
    OofCandidates candidates = output->oofCandidates;
    output->oofCandidates.Clear();

    Vec<NodeId> hoisted;
    OofCandidates unclaimed;
    RectF overflow = RectF::Zero();
    PerformOofLayout(tree, node, candidates, output->oofPositioningArea.size,
                     output->oofPositioningArea.offset, direction, claims,
                     isScrollContainer, &hoisted, &unclaimed, &overflow);
    tree->ClearHoistedChildren(node);
    tree->AddHoistedChildren(node, hoisted);
    output->oofCandidates = unclaimed;
    output->scrollableOverflowRect = output->scrollableOverflowRect
                                         .Union(overflow);
}

} // namespace taffy
