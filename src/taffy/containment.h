/* CSS size containment — taffy/src/compute/common/containment.rs.
 *
 * Included from compute.h. The header opens its own namespace: the amalgam
 * lifts this file out of compute.h, so it cannot rely on being included
 * inside `namespace taffy`.
 *
 * https://drafts.csswg.org/css-contain-2/#containment-size
 */

#ifndef GPUI_TAFFY_CONTAINMENT_H_
#define GPUI_TAFFY_CONTAINMENT_H_

namespace taffy {

// Whether each axis is definite for resolving children's percentages.
// A size computed by the as-if-empty pass is a used size, not a definite one.
// https://github.com/w3c/csswg-drafts/issues/7206
inline void ContainedSizeIsDefinite(const Style& style,
                                    const LayoutInput& inputs,
                                    CalcResolver calc, bool* outW, bool* outH) {
    if (IsSome(inputs.knownDimensions.w) && IsSome(inputs.knownDimensions.h)) {
        *outW = inputs.knownDimensionsAreDefiniteW;
        *outH = inputs.knownDimensionsAreDefiniteH;
        return;
    }

    SizeFOpt parentSize = inputs.parentSize;
    Optf aspectRatio = style.aspectRatio;
    SizeFOpt styleSize = SizeFOptNone();
    if (inputs.sizingMode == SizingMode::InherentSize) {
        styleSize = MaybeApplyAspectRatio(
            style.size.MaybeResolve(parentSize, calc), aspectRatio);
    }
    SizeFOpt minSize = MaybeApplyAspectRatio(
        style.minSize.MaybeResolve(parentSize, calc), aspectRatio);
    SizeFOpt maxSize = MaybeApplyAspectRatio(
        style.maxSize.MaybeResolve(parentSize, calc), aspectRatio);

    SizeFOpt minMaxDefinite = SizeFOptNone();
    if (IsSome(minSize.w) && IsSome(maxSize.w) && maxSize.w <= minSize.w) {
        minMaxDefinite.w = minSize.w;
    }
    if (IsSome(minSize.h) && IsSome(maxSize.h) && maxSize.h <= minSize.h) {
        minMaxDefinite.h = minSize.h;
    }
    SizeFOpt styled = Or(styleSize, minMaxDefinite);
    bool styledW = IsSome(styled.w);
    bool styledH = IsSome(styled.h);

    bool defW = IsSome(inputs.knownDimensions.w)
                    ? inputs.knownDimensionsAreDefiniteW
                    : styledW;
    bool defH = IsSome(inputs.knownDimensions.h)
                    ? inputs.knownDimensionsAreDefiniteH
                    : styledH;

    // aspect-ratio transfers definiteness into an axis that has no known
    // dimension. Both transfers read the pre-transfer flags.
    if (IsSome(aspectRatio)) {
        bool widthKnown = IsSome(inputs.knownDimensions.w);
        bool heightKnown = IsSome(inputs.knownDimensions.h);
        bool nextW = defW || (!widthKnown && defH);
        bool nextH = defH || (!heightKnown && defW);
        defW = nextW;
        defH = nextH;
    }
    *outW = defW;
    *outH = defH;
}

// Size containment in two phases: size the box as if it had no children, then
// lay the children out into that fixed size. An auto axis's used size is not
// definite for percentage resolution. Inline-size containment does the same
// for the horizontal axis only (horizontal-tb is the only writing mode).
// `computeInner`'s bool is ignore-children.
template <typename Fn>
LayoutOutput ComputeContainedSizeLayout(TaffyTree* tree, NodeId node,
                                        LayoutInput inputs, Contain contain,
                                        bool definiteW, bool definiteH,
                                        Fn computeInner) {
    if (contain.Contains(Contain::Size())) {
        SizeFOpt known = inputs.knownDimensions;
        SizeF size;
        if (IsSome(known.w) && IsSome(known.h)) {
            size = {known.w, known.h};
        } else {
            LayoutInput emptyIn = inputs;
            emptyIn.runMode = RunMode::ComputeSize;
            emptyIn.axis = RequestedAxis::Both;
            SizeF emptySize = computeInner(tree, node, emptyIn, true).size;
            size.w = IsSome(known.w) ? known.w : emptySize.w;
            size.h = IsSome(known.h) ? known.h : emptySize.h;
        }
        if (inputs.runMode == RunMode::ComputeSize) {
            return LayoutOutput::FromOuterSize(size);
        }
        LayoutInput phase2 = inputs;
        phase2.knownDimensions = {Some(size.w), Some(size.h)};
        phase2.knownDimensionsAreDefiniteW = definiteW;
        phase2.knownDimensionsAreDefiniteH = definiteH;
        LayoutOutput output = computeInner(tree, node, phase2, false);
        output.size = size;
        return output;
    }

    float width;
    if (IsSome(inputs.knownDimensions.w)) {
        width = inputs.knownDimensions.w;
    } else {
        LayoutInput emptyIn = inputs;
        emptyIn.runMode = RunMode::ComputeSize;
        emptyIn.axis = RequestedAxis::Horizontal;
        width = computeInner(tree, node, emptyIn, true).size.w;
    }
    if (inputs.runMode == RunMode::ComputeSize &&
        inputs.axis == RequestedAxis::Horizontal) {
        return LayoutOutput::FromOuterSize({width, 0.0f});
    }
    LayoutInput phase2 = inputs;
    phase2.knownDimensions.w = Some(width);
    phase2.knownDimensionsAreDefiniteW = definiteW;
    phase2.knownDimensionsAreDefiniteH = inputs.knownDimensionsAreDefiniteH;
    return computeInner(tree, node, phase2, false);
}

} // namespace taffy

#endif // GPUI_TAFFY_CONTAINMENT_H_
