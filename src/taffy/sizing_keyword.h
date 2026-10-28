/* Resolution of min-content, max-content, fit-content, fit-content(...) and
 * stretch on width and height. taffy/src/compute/common/sizing_keyword.rs.
 *
 * An unresolved keyword behaves as auto. Parse and serde of the keywords are
 * omitted with those features.
 */

#ifndef GPUI_TAFFY_SIZING_KEYWORD_H_
#define GPUI_TAFFY_SIZING_KEYWORD_H_

#include "taffy/taffy_math.h"
#include "taffy/taffy_tree.h"

namespace taffy {

enum class SizingKeywordKind : uint8_t {
    Measure,
    Exact
};

// None when the style is not a sizing keyword, or the keyword cannot be
// resolved in this context.
struct SizingKeywordResolution {
    bool isSome = false;
    SizingKeywordKind kind = SizingKeywordKind::Measure;
    AvailableSpace measure = AvailableSpace::MaxContent();
    float exact = 0.0f;
};

// `stretchSize` is the size the item would take if it filled the available
// space (available minus margins). `percentBasis` resolves a percentage or
// calc() fit-content() limit.
inline SizingKeywordResolution ResolveSizingKeyword(Dimension style,
                                                    Optf stretchSize,
                                                    Optf percentBasis,
                                                    CalcResolver calc) {
    SizingKeywordResolution out;
    switch (style.raw.Tag()) {
        case CompactLength::kMinContentTag:
            out.isSome = true;
            out.kind = SizingKeywordKind::Measure;
            out.measure = AvailableSpace::MinContent();
            break;
        case CompactLength::kMaxContentTag:
            out.isSome = true;
            out.kind = SizingKeywordKind::Measure;
            out.measure = AvailableSpace::MaxContent();
            break;
        case CompactLength::kFitContentPxTag:
            out.isSome = true;
            out.kind = SizingKeywordKind::Measure;
            out.measure = AvailableSpace::Definite(style.Value());
            break;
        case CompactLength::kFitContentPercentTag:
            if (IsSome(percentBasis)) {
                out.isSome = true;
                out.kind = SizingKeywordKind::Measure;
                out.measure =
                    AvailableSpace::Definite(percentBasis * style.Value());
            }
            break;
        case CompactLength::kFitContentCalcTag:
            if (IsSome(percentBasis)) {
                out.isSome = true;
                out.kind = SizingKeywordKind::Measure;
                out.measure = AvailableSpace::Definite(
                    calc.Resolve(style.raw.CalcValue(), percentBasis));
            }
            break;
        case CompactLength::kFitContentKeywordTag:
            if (IsSome(stretchSize)) {
                out.isSome = true;
                out.kind = SizingKeywordKind::Measure;
                out.measure = AvailableSpace::Definite(stretchSize);
            }
            break;
        case CompactLength::kStretchTag:
            if (IsSome(stretchSize)) {
                out.isSome = true;
                out.kind = SizingKeywordKind::Exact;
                out.exact = stretchSize;
            }
            break;
        default:
            break;
    }
    return out;
}

// Fills `knownDimensions` for an absolutely positioned item. An axis that
// already has a size is left alone. `areaSize` is the containing block.
inline void ResolveAbsoluteSizingKeywords(TaffyTree* tree, NodeId node,
                                          SizeFOpt* knownDimensions,
                                          SizeDim sizeStyle, SizeF areaSize,
                                          RectFOpt inset, RectFOpt margin,
                                          SizingMode sizingMode) {
    float stretchW = F32Max(
        areaSize.w - UnwrapOr(inset.left, 0.0f) - UnwrapOr(inset.right, 0.0f) -
            UnwrapOr(margin.left, 0.0f) - UnwrapOr(margin.right, 0.0f),
        0.0f);
    float stretchH = F32Max(
        areaSize.h - UnwrapOr(inset.top, 0.0f) - UnwrapOr(inset.bottom, 0.0f) -
            UnwrapOr(margin.top, 0.0f) - UnwrapOr(margin.bottom, 0.0f),
        0.0f);

    SizingKeywordResolution keywordWidth;
    if (!IsSome(knownDimensions->w)) {
        keywordWidth = ResolveSizingKeyword(sizeStyle.width, Some(stretchW),
                                            Some(areaSize.w), tree->calc);
    }
    SizingKeywordResolution keywordHeight;
    if (!IsSome(knownDimensions->h)) {
        keywordHeight = ResolveSizingKeyword(sizeStyle.height, Some(stretchH),
                                             Some(areaSize.h), tree->calc);
    }

    bool bothMeasure = keywordWidth.isSome && keywordHeight.isSome &&
                       keywordWidth.kind == SizingKeywordKind::Measure &&
                       keywordHeight.kind == SizingKeywordKind::Measure;
    if (bothMeasure) {
        SizeF measured = tree->MeasureChildSizeBoth(
            node, SizeFOptNone(), AsOptional(areaSize),
            {keywordWidth.measure, keywordHeight.measure}, sizingMode,
            LineBool::False());
        *knownDimensions = AsOptional(measured);
        return;
    }

    if (keywordWidth.isSome) {
        if (keywordWidth.kind == SizingKeywordKind::Exact) {
            knownDimensions->w = Some(keywordWidth.exact);
        } else {
            knownDimensions->w = Some(tree->MeasureChildSize(
                node, *knownDimensions, AsOptional(areaSize),
                {keywordWidth.measure, AvailableSpace::Definite(stretchH)},
                sizingMode, AbsoluteAxis::Horizontal, LineBool::False()));
        }
    }
    if (keywordHeight.isSome) {
        if (keywordHeight.kind == SizingKeywordKind::Exact) {
            knownDimensions->h = Some(keywordHeight.exact);
        } else {
            AvailableSpace widthAvail =
                IsSome(knownDimensions->w)
                    ? AvailableSpace::Definite(knownDimensions->w)
                    : AvailableSpace::Definite(stretchW);
            knownDimensions->h = Some(tree->MeasureChildSize(
                node, *knownDimensions, AsOptional(areaSize),
                {widthAvail, keywordHeight.measure}, sizingMode,
                AbsoluteAxis::Vertical, LineBool::False()));
        }
    }
}

} // namespace taffy

#endif // GPUI_TAFFY_SIZING_KEYWORD_H_
