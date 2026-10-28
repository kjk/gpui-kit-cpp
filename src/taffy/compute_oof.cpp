/* Out-of-flow positioning — taffy/src/compute/oof.rs.
 *
 * Block, flex and grid only collect candidates. This pass claims the ones
 * whose containing block is the current node and lays them out against that
 * node's positioning area. `position: fixed` is left for the root.
 */

#include "taffy/compute.h"
#include "taffy/sizing_keyword.h"

namespace taffy {

// Position a margin box of `size` inside [start, end] against a physical edge.
// The returned value is the margin box's start, not the border box's.
static float AlignInLine(float start, float end, float size,
                         AxisStaticEdge edge) {
    if (edge == AxisStaticEdge::End) {
        return end - size;
    }
    if (edge == AxisStaticEdge::Center) {
        return (start + end - size) / 2.0f;
    }
    return start;
}

float ResolveStaticOffsetAxis(AxisStaticPosition position, float size,
                              float marginStart, float marginEnd) {
    float marginBox = size + marginStart + marginEnd;
    bool overflows = position.align.safety == AlignmentSafety::Safe &&
                     marginBox > position.end - position.start;
    AxisStaticEdge keyword =
        overflows ? position.align.fallback : position.align.keyword;
    return AlignInLine(position.start, position.end, marginBox, keyword) +
           marginStart;
}

// Self-alignment of an out-of-flow box, logical to the containing block.
// Normal and Stretch position as Start once the size is known.
enum class OofAlignment : uint8_t {
    Normal,
    Stretch,
    Start,
    Center,
    End
};

static OofAlignment OofAlignmentFromStyle(OptAlignItems alignment,
                                          Direction itemDirection,
                                          Direction containerDirection,
                                          bool axisIsInline) {
    if (!alignment.IsSome()) {
        return OofAlignment::Normal;
    }
    AlignItemsKeyword keyword =
        ResolveSelfRelative(alignment.val, itemDirection, containerDirection,
                            axisIsInline)
            .keyword;
    switch (keyword) {
        case AlignItemsKeyword::Stretch:
            return OofAlignment::Stretch;
        case AlignItemsKeyword::Center:
            return OofAlignment::Center;
        case AlignItemsKeyword::End:
        case AlignItemsKeyword::FlexEnd:
        case AlignItemsKeyword::SelfEnd:
            return OofAlignment::End;
        case AlignItemsKeyword::Start:
        case AlignItemsKeyword::FlexStart:
        case AlignItemsKeyword::SelfStart:
        case AlignItemsKeyword::Baseline:
        default:
            return OofAlignment::Start;
    }
}

// End flips when the axis is reversed. Normal, Stretch and Start flip the
// other way, so a reversed start edge is the physical end.
static AxisStaticEdge OofPhysicalEdge(OofAlignment alignment, bool reversed) {
    if (alignment == OofAlignment::Center) {
        return AxisStaticEdge::Center;
    }
    bool isEnd = alignment == OofAlignment::End;
    return (isEnd != reversed) ? AxisStaticEdge::End : AxisStaticEdge::Start;
}

// One axis of the absolute positioning model. Coordinates are physical and
// relative to the start of the inset-resolution area.
struct OofAxis {
    float cbSize = 0.0f;
    Optf insetStart = None();
    Optf insetEnd = None();
    Optf marginStart = None();
    Optf marginEnd = None();
    AxisStaticPosition staticPosition;
    OofAlignment alignment = OofAlignment::Normal;
    AlignmentSafety safety = AlignmentSafety::Default;
    bool isInline = false;
    bool reversed = false;
    bool isScrollAxis = false;

    AxisStaticEdge StartEdge() const {
        return reversed ? AxisStaticEdge::End : AxisStaticEdge::Start;
    }
    AxisStaticEdge EndEdge() const {
        return reversed ? AxisStaticEdge::Start : AxisStaticEdge::End;
    }

    // https://www.w3.org/TR/css-position-3/#resolving-insets
    void InsetModifiedContainingBlock(float* start, float* end) const {
        AxisStaticEdge weaker;
        if (IsSome(insetStart) && IsSome(insetEnd)) {
            *start = insetStart;
            *end = cbSize - insetEnd;
            weaker = EndEdge();
        } else if (IsSome(insetStart)) {
            *start = insetStart;
            *end = cbSize;
            weaker = AxisStaticEdge::End;
        } else if (IsSome(insetEnd)) {
            *start = 0.0f;
            *end = cbSize - insetEnd;
            weaker = AxisStaticEdge::Start;
        } else {
            AxisStaticEdge keyword = staticPosition.align.keyword;
            if (keyword == AxisStaticEdge::End) {
                *start = 0.0f;
                *end = staticPosition.end;
            } else if (keyword == AxisStaticEdge::Center) {
                float center =
                    (staticPosition.start + staticPosition.end) / 2.0f;
                float startDistance = center;
                float endDistance = cbSize - center;
                float startAbs =
                    startDistance < 0.0f ? -startDistance : startDistance;
                float endAbs = endDistance < 0.0f ? -endDistance : endDistance;
                if (startAbs <= endAbs) {
                    *start = 0.0f;
                    *end = 2.0f * startDistance;
                } else {
                    *start = cbSize - 2.0f * endDistance;
                    *end = cbSize;
                }
            } else {
                *start = staticPosition.start;
                *end = cbSize;
            }
            weaker = EndEdge();
        }
        if (*end < *start) {
            if (weaker == AxisStaticEdge::Start) {
                *start = *end;
            } else {
                *end = *start;
            }
        }
    }

    float InsetModifiedContainingBlockSize() const {
        float start = 0.0f;
        float end = 0.0f;
        InsetModifiedContainingBlock(&start, &end);
        return end - start;
    }

    // Border-box stretch-fit: the IMCB minus non-auto margins.
    float StretchFitSize() const {
        float margins = UnwrapOr(marginStart, 0.0f) + UnwrapOr(marginEnd, 0.0f);
        return F32Max(InsetModifiedContainingBlockSize() - margins, 0.0f);
    }

    bool IsStretchSized(bool isReplaced, bool isTable) const {
        if (!(IsSome(insetStart) && IsSome(insetEnd))) {
            return false;
        }
        if (alignment == OofAlignment::Stretch) {
            return true;
        }
        if (alignment == OofAlignment::Normal) {
            return !isReplaced && !isTable;
        }
        return false;
    }

    // https://www.w3.org/TR/css-position-3/#abspos-margins
    void ResolveMargins(float size, float* start, float* end) const {
        bool bothInsets = IsSome(insetStart) && IsSome(insetEnd);
        float imcbStart = 0.0f;
        float imcbEnd = 0.0f;
        InsetModifiedContainingBlock(&imcbStart, &imcbEnd);
        float freeSpace = imcbEnd - imcbStart - size;
        if (IsSome(marginStart) && IsSome(marginEnd)) {
            *start = marginStart;
            *end = marginEnd;
            return;
        }
        if (!bothInsets) {
            *start = UnwrapOr(marginStart, 0.0f);
            *end = UnwrapOr(marginEnd, 0.0f);
            return;
        }
        if (IsSome(marginStart)) {
            *start = marginStart;
            *end = freeSpace - marginStart;
            return;
        }
        if (IsSome(marginEnd)) {
            *end = marginEnd;
            *start = freeSpace - marginEnd;
            return;
        }
        // Inline axis: negative free space sits entirely on the end margin.
        if (freeSpace < 0.0f && isInline) {
            if (reversed) {
                *start = freeSpace;
                *end = 0.0f;
            } else {
                *start = 0.0f;
                *end = freeSpace;
            }
            return;
        }
        *start = freeSpace / 2.0f;
        *end = freeSpace / 2.0f;
    }

    // Border-box start. https://www.w3.org/TR/css-position-3/#abspos-alignment
    float Position(float size, float marginStartResolved,
                   float marginEndResolved) const {
        float imcbStart = 0.0f;
        float imcbEnd = 0.0f;
        InsetModifiedContainingBlock(&imcbStart, &imcbEnd);
        if (!IsSome(insetStart) && !IsSome(insetEnd)) {
            return ResolveStaticOffsetAxis(
                staticPosition, size, marginStartResolved, marginEndResolved);
        }
        if (IsSome(insetStart) && !IsSome(insetEnd)) {
            return imcbStart + marginStartResolved;
        }
        if (!IsSome(insetStart) && IsSome(insetEnd)) {
            return imcbEnd - size - marginEndResolved;
        }
        if (!IsSome(marginStart) || !IsSome(marginEnd)) {
            return imcbStart + marginStartResolved;
        }
        float marginBox = size + marginStartResolved + marginEndResolved;
        float imcbSize = imcbEnd - imcbStart;
        AxisStaticEdge edge = OofPhysicalEdge(alignment, reversed);
        float marginBoxStart;
        if (safety == AlignmentSafety::Unsafe) {
            marginBoxStart = AlignInLine(imcbStart, imcbEnd, marginBox, edge);
        } else if (safety == AlignmentSafety::Safe) {
            AxisStaticEdge used = marginBox > imcbSize ? StartEdge() : edge;
            marginBoxStart = AlignInLine(imcbStart, imcbEnd, marginBox, used);
        } else if (marginBox <= imcbSize || alignment == OofAlignment::Normal) {
            marginBoxStart = AlignInLine(imcbStart, imcbEnd, marginBox, edge);
        } else {
            // Overflow limit: the union of the IMCB and the containing
            // block, extended to infinity along a scroll axis.
            float limitStart = F32Min(imcbStart, 0.0f);
            float limitEnd = F32Max(imcbEnd, cbSize);
            if (isScrollAxis) {
                if (reversed) {
                    limitStart = -INFINITY;
                } else {
                    limitEnd = INFINITY;
                }
            }
            if (marginBox <= limitEnd - limitStart) {
                float aligned =
                    AlignInLine(imcbStart, imcbEnd, marginBox, edge);
                marginBoxStart =
                    F32Max(F32Min(aligned, limitEnd - marginBox), limitStart);
            } else {
                marginBoxStart =
                    AlignInLine(limitStart, limitEnd, marginBox, StartEdge());
            }
        }
        return marginBoxStart + marginStartResolved;
    }
};

OofBoxLayout LayoutOofBox(TaffyTree* tree, const OofCandidate& candidate,
                          SizeF areaSize, PointF areaOffset,
                          Direction direction, bool resolveGridArea,
                          NodeId gridOwner, PointOverflow containerOverflow,
                          bool* cacheHit) {
    OofBoxLayout result;
    SizeF usedArea = areaSize;
    PointF usedOffset = areaOffset;
    if (resolveGridArea) {
        RectF paddingBox = {areaOffset.x, areaOffset.x + areaSize.w,
                            areaOffset.y, areaOffset.y + areaSize.h};
        RectF gridArea;
        if (GridOofArea(tree, gridOwner, candidate.node, paddingBox,
                        &gridArea)) {
            usedArea = {gridArea.right - gridArea.left,
                        gridArea.bottom - gridArea.top};
            usedOffset = {gridArea.left, gridArea.top};
        }
    }
    float areaWidth = usedArea.w;
    float areaHeight = usedArea.h;

    CalcResolver calc = tree->calc;
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

    SizeFOpt styleSize = MaybeAdd(
        MaybeApplyAspectRatio(
            sizeStyle.MaybeResolve(AsOptional(usedArea), calc), aspectRatio),
        boxSizingAdjustment);
    SizeFOpt minSize = MaybeMax(
        Or(MaybeAdd(MaybeApplyAspectRatio(
                        cs.minSize.MaybeResolve(AsOptional(usedArea), calc),
                        aspectRatio),
                    boxSizingAdjustment),
           AsOptional(paddingBorderSum)),
        paddingBorderSum);
    SizeFOpt maxSize = MaybeAdd(
        MaybeApplyAspectRatio(
            cs.maxSize.MaybeResolve(AsOptional(usedArea), calc), aspectRatio),
        boxSizingAdjustment);
    SizeFOpt knownDimensions = MaybeClamp(styleSize, minSize, maxSize);
    // An absolute replaced box keeps its intrinsic size. A table is not
    // stretch-sized by `normal` either. Explicit stretch still applies.
    bool isReplaced = cs.IsCompressibleReplaced();
    bool isTable = cs.itemIsTable;
    Direction itemDirection = cs.direction;
    OptAlignSelf justifySelf = cs.justifySelf;
    OptAlignSelf alignSelf = cs.alignSelf;

    // Static positions arrive relative to the containing block's border box.
    // The inset-resolution area starts at `usedOffset`.
    AxisStaticPosition staticX = candidate.staticX;
    staticX.start -= usedOffset.x;
    staticX.end -= usedOffset.x;
    AxisStaticPosition staticY = candidate.staticY;
    staticY.start -= usedOffset.y;
    staticY.end -= usedOffset.y;

    OofAxis horizontal;
    horizontal.cbSize = areaWidth;
    horizontal.insetStart = left;
    horizontal.insetEnd = right;
    horizontal.marginStart = margin.left;
    horizontal.marginEnd = margin.right;
    horizontal.staticPosition = staticX;
    horizontal.alignment =
        OofAlignmentFromStyle(justifySelf, itemDirection, direction, true);
    horizontal.safety = justifySelf.IsSome() ? justifySelf.val.safety
                                             : AlignmentSafety::Default;
    horizontal.isInline = true;
    horizontal.reversed = IsRtl(direction);
    horizontal.isScrollAxis = IsScrollContainer(containerOverflow.x);

    OofAxis vertical;
    vertical.cbSize = areaHeight;
    vertical.insetStart = top;
    vertical.insetEnd = bottom;
    vertical.marginStart = margin.top;
    vertical.marginEnd = margin.bottom;
    vertical.staticPosition = staticY;
    vertical.alignment =
        OofAlignmentFromStyle(alignSelf, itemDirection, direction, false);
    vertical.safety =
        alignSelf.IsSome() ? alignSelf.val.safety : AlignmentSafety::Default;
    vertical.isInline = false;
    vertical.reversed = false;
    vertical.isScrollAxis = IsScrollContainer(containerOverflow.y);

    float stretchWidth = horizontal.StretchFitSize();
    float stretchHeight = vertical.StretchFitSize();
    // A table's available space never exceeds the containing block minus
    // its non-auto margins. https://www.w3.org/TR/css-tables-3/#abspos
    float availWidth = stretchWidth;
    float availHeight = stretchHeight;
    if (isTable) {
        float nonAutoW =
            UnwrapOr(margin.left, 0.0f) + UnwrapOr(margin.right, 0.0f);
        float nonAutoH =
            UnwrapOr(margin.top, 0.0f) + UnwrapOr(margin.bottom, 0.0f);
        availWidth = F32Min(availWidth, F32Max(areaWidth - nonAutoW, 0.0f));
        availHeight = F32Min(availHeight, F32Max(areaHeight - nonAutoH, 0.0f));
    }
    SizeFOpt availClamped =
        MaybeClamp(SizeFOpt{availWidth, availHeight}, minSize, maxSize);
    SizeAvail childAvail =
        SizeAvail::Definite({availClamped.w, availClamped.h});

    // An explicitly sized axis wins over the automatic size below.
    if (sizeStyle.width.IsSizingKeyword() || sizeStyle.height
                                                 .IsSizingKeyword()) {
        ResolveAbsoluteSizingKeywords(tree, candidate.node, &knownDimensions,
                                      sizeStyle, usedArea, inset, margin,
                                      SizingMode::ContentSize);
        knownDimensions =
            MaybeClamp(MaybeApplyAspectRatio(knownDimensions, aspectRatio),
                       minSize, maxSize);
    }

    // Automatic size: stretch-fit when alignment is stretch, or normal for
    // a non-replaced non-table box, and neither inset is auto. Otherwise
    // the size is fit-content and is measured below. Width is resolved
    // first so an aspect ratio can transfer it to the block axis. Explicit
    // stretch on both axes ignores the aspect ratio.
    bool stretchW = !IsSome(knownDimensions.w) &&
                    horizontal.IsStretchSized(isReplaced, isTable);
    bool stretchH = !IsSome(knownDimensions.h) &&
                    vertical.IsStretchSized(isReplaced, isTable);
    bool bothExplicitStretch = horizontal.alignment == OofAlignment::Stretch &&
                               vertical.alignment == OofAlignment::Stretch;
    Optf stretchAspect =
        (stretchW && stretchH && bothExplicitStretch) ? None() : aspectRatio;
    if (stretchW) {
        if (isTable) {
            minSize.w = Some(F32Max(UnwrapOr(minSize.w, 0.0f), stretchWidth));
        } else {
            knownDimensions.w = Some(stretchWidth);
            knownDimensions = MaybeClamp(
                MaybeApplyAspectRatio(knownDimensions, stretchAspect), minSize,
                maxSize);
        }
    }
    if (!IsSome(knownDimensions.h) && stretchH) {
        if (isTable) {
            minSize.h = Some(F32Max(UnwrapOr(minSize.h, 0.0f), stretchHeight));
        } else {
            knownDimensions.h = Some(stretchHeight);
            knownDimensions = MaybeClamp(
                MaybeApplyAspectRatio(knownDimensions, stretchAspect), minSize,
                maxSize);
        }
    }

    SizeF finalSize;
    if (IsSome(knownDimensions.w) && IsSome(knownDimensions.h)) {
        finalSize = {knownDimensions.w, knownDimensions.h};
    } else {
        SizeF measured = tree->MeasureChildSizeBoth(
            candidate.node, knownDimensions, AsOptional(usedArea), childAvail,
            SizingMode::ContentSize, LineBool::False());
        finalSize = UnwrapOr(knownDimensions, measured);
    }
    finalSize = MaybeClamp(finalSize, minSize, maxSize);

    LayoutInput layoutInput;
    layoutInput.knownDimensions = AsOptional(finalSize);
    layoutInput.parentSize = AsOptional(usedArea);
    layoutInput.availableSpace = childAvail;
    layoutInput.sizingMode = SizingMode::ContentSize;
    layoutInput.axis = RequestedAxis::Both;
    layoutInput.runMode = RunMode::PerformLayout;
    layoutInput.verticalMarginsAreCollapsible = LineBool::False();
    if (cacheHit) {
        LayoutOutput probe;
        *cacheHit = tree->CacheGet(candidate.node, layoutInput, &probe);
    }
    LayoutOutput layoutOutput =
        tree->ComputeChildLayout(candidate.node, layoutInput);

    float marginLeft = 0.0f;
    float marginRight = 0.0f;
    float marginTop = 0.0f;
    float marginBottom = 0.0f;
    horizontal.ResolveMargins(finalSize.w, &marginLeft, &marginRight);
    vertical.ResolveMargins(finalSize.h, &marginTop, &marginBottom);
    RectF resolvedMargin = {marginLeft, marginRight, marginTop, marginBottom};
    PointF location = {
        horizontal.Position(finalSize.w, marginLeft, marginRight) + usedOffset
                                                                        .x,
        vertical.Position(finalSize.h, marginTop, marginBottom) + usedOffset.y};

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

    // Anchors stay relative to this box. The caller translates them into its
    // own border box. The root pass must see them unshifted.
    if (!layoutOutput.oofCandidates.IsEmpty()) {
        result.surfaced = layoutOutput.oofCandidates;
    }

    float relX = location.x - usedOffset.x;
    PointF relativeLocation = {
        IsRtl(direction) ? usedArea.w - relX - finalSize.w : relX,
        location.y - usedOffset.y};
    result.layout = layout;
    bool isScrollContainer = IsScrollContainer(containerOverflow.x) ||
                             IsScrollContainer(containerOverflow.y);
    result.overflowContribution = ComputeScrollableOverflowContribution(
        relativeLocation, finalSize, layoutOutput.scrollableOverflowRect,
        overflow, contain, isScrollContainer);
    return result;
}

void PerformOofLayout(TaffyTree* tree, NodeId nodeId, OofCandidates candidates,
                      SizeF areaSize, PointF areaOffset, Direction direction,
                      ContainingBlockClaims claims,
                      PointOverflow containerOverflow, Vec<NodeId>* hoisted,
                      OofCandidates* unclaimed, RectF* overflowOut) {
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

    int index = 0;
    while (index < len(work)) {
        OofCandidate candidate = work[index];
        index += 1;
        VecAppend(*hoisted, candidate.node);
        OofBoxLayout box =
            LayoutOofBox(tree, candidate, areaSize, areaOffset, direction, true,
                         nodeId, containerOverflow, nullptr);
        if (!box.surfaced.IsEmpty()) {
            box.surfaced.Translate(box.layout.location);
        }
        for (int s = 0; s < len(box.surfaced.inner); s++) {
            const OofCandidate& surfaced = box.surfaced.inner[s];
            if (claims.ForPosition(surfaced.position)) {
                VecAppend(work, surfaced);
            } else {
                unclaimed->Push(surfaced);
            }
        }
        absoluteOverflowRect = absoluteOverflowRect
                                   .Union(box.overflowContribution);
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
    OofCandidates candidates = output->oofCandidates;
    output->oofCandidates.Clear();

    Vec<NodeId> hoisted;
    OofCandidates unclaimed;
    RectF overflow = RectF::Zero();
    PerformOofLayout(tree, node, candidates, output->oofPositioningArea.size,
                     output->oofPositioningArea.offset, direction, claims,
                     style.overflow, &hoisted, &unclaimed, &overflow);
    tree->ClearHoistedChildren(node);
    tree->AddHoistedChildren(node, hoisted);
    output->oofCandidates = unclaimed;
    output->scrollableOverflowRect = output->scrollableOverflowRect
                                         .Union(overflow);
}

} // namespace taffy
