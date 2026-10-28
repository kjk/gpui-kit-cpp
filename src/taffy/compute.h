/* The layout algorithms — everything under taffy/src/compute.
 *
 * Rust's compute functions take `&mut impl LayoutPartialTree`; here they take
 * a `TaffyTree*`, since there is one tree type (see taffy_tree.h).
 *
 * `compute_cached_layout` takes a closure in Rust and has exactly one caller.
 * That caller is `TaffyTree::ComputeChildLayout`, and the cache check lives
 * inline there rather than in a function taking a callback.
 */

#ifndef GPUI_TAFFY_COMPUTE_H_
#define GPUI_TAFFY_COMPUTE_H_

#include "taffy/taffy_math.h"
#include "taffy/taffy_tree.h"

namespace taffy {

// ─── entry points ────────────────────────────────────────────────────────

// Lay out the root node of a tree, whatever its layout mode. Called once to
// begin a layout run.
void ComputeRootLayout(TaffyTree* tree, NodeId root, SizeAvail availableSpace);

// Mark a node and everything under it as hidden: zero size, at the origin.
LayoutOutput ComputeHiddenLayout(TaffyTree* tree, NodeId node);

// Round a tree of float-valued layouts onto the pixel grid. Reads
// `unroundedLayout` and writes `finalLayout`, so rounding never compounds.
void RoundLayout(TaffyTree* tree, NodeId node);

// Lay out the candidates in `output` for which `node` is the containing
// block. PerformLayout only, and only when the positioning area is set.
// Called inside the cache-miss path so a hit already carries the remainder.
void ComputeOofLayout(TaffyTree* tree, NodeId node, LayoutOutput* output);

// Lays out `candidates`, appending claimed nodes to `hoisted` and the rest
// to `unclaimed`. `overflowOut` is the scrollable overflow of the claimed
// boxes, relative to the positioning area's origin.
void PerformOofLayout(TaffyTree* tree, NodeId nodeId, OofCandidates candidates,
                      SizeF areaSize, PointF areaOffset, Direction direction,
                      ContainingBlockClaims claims, bool isScrollContainer,
                      Vec<NodeId>* hoisted, OofCandidates* unclaimed,
                      RectF* overflowOut);

// Static-position alignment for one axis. Exported for the unit tests.
float ResolveStaticOffsetAxis(AxisStaticPosition position, float size,
                              float marginStart, float marginEnd);

// The grid area of `child` inside `grid`, or false when `grid` has no
// track snapshot. `paddingBox` is the inset-resolution area (border box
// minus borders and scrollbar gutters).
bool GridOofArea(TaffyTree* tree, NodeId grid, NodeId child, RectF paddingBox,
                 RectF* out);

// Measures a leaf's intrinsic size. Rust passes a closure; the void* is what
// it would have captured.
using LeafMeasureFn = SizeF (*)(SizeFOpt knownDimensions,
                                SizeAvail availableSpace, void* ctx);

// Apply padding, border and aspect-ratio to a childless node, then defer to
// `measure` for its size. This is the text and image node path.
LayoutOutput ComputeLeafLayout(const LayoutInput& inputs, const Style& style,
                               CalcResolver calc, LeafMeasureFn measure,
                               void* measureCtx);

LayoutOutput ComputeFlexboxLayout(TaffyTree* tree, NodeId node,
                                  const LayoutInput& inputs);

LayoutOutput ComputeGridLayout(TaffyTree* tree, NodeId node,
                               const LayoutInput& inputs);

LayoutOutput ComputeBlockLayout(TaffyTree* tree, NodeId node,
                                const LayoutInput& inputs,
                                BlockContext* blockCtx);

// ─── shared CSS alignment — taffy/src/compute/common/alignment.rs ────────

// The `safe`/`unsafe` overflow-position fallback for a self-level alignment
// value (align-self / justify-self, and absolutely positioned items). A
// `safe` alignment whose subject overflows falls back to Start.
inline AlignItemsKeyword ResolveSelfAlignmentSafety(AlignItems alignment,
                                                    bool overflows) {
    if (alignment.safety == AlignmentSafety::Safe && overflows) {
        return AlignItemsKeyword::Start;
    }
    return alignment.keyword;
}

// The spec-defined fallbacks for an AlignContent value, returning the bare
// keyword the alignment math should use. Follows
// https://www.w3.org/TR/css-align-3/ plus the resolution of
// https://github.com/w3c/csswg-drafts/issues/10154.
AlignContentKeyword ApplyAlignmentFallback(float freeSpace, int numItems,
                                           AlignContent alignmentMode);

// Used for both align-content and justify-content, in both flexbox and grid.
// CSS Grid does not apply gaps as part of alignment, so grid passes gap = 0.
float ComputeAlignmentOffset(float freeSpace, int numItems, float gap,
                             AlignContentKeyword alignmentMode,
                             bool layoutIsFlexReversed, bool isFirst);

// ─── grid test seams ─────────────────────────────────────────────────────
//
// The CSS Grid internals are private to compute_grid.cpp, but taffy's own unit
// tests reach into three of them. AGENTS.md says to add the seam rather than
// the harness, so these four calls exist for tests/TaffyTests.cpp and have no
// other caller.

// `compute_explicit_grid_size_in_axis`. `maxRepetitions` picks between the
// two `AutoRepeatStrategy` values.
void GridExplicitSizeForTest(const Style& style, Optf autoFitContainerSize,
                             bool maxRepetitions, AbsoluteAxis axis,
                             CalcResolver calc, uint16_t* outAutoRepetitions,
                             uint16_t* outTrackCount);

// `child_min_line_max_line_span`, in OriginZero coordinates.
void GridChildMinMaxSpanForTest(LinePlacement line, uint16_t explicitTrackCount,
                                int16_t* outMinLine, int16_t* outMaxLine,
                                uint16_t* outSpan);

// `compute_grid_size_estimate`. Each of the three out arrays takes the
// negative-implicit, explicit and positive-implicit counts.
void GridSizeEstimateForTest(uint16_t explicitColCount,
                             uint16_t explicitRowCount,
                             const LinePlacement* columns,
                             const LinePlacement* rows, int n,
                             uint16_t* outColCounts, uint16_t* outRowCounts);

// One initialised track, as `initialize_grid_tracks` produced it: whether it
// is a gutter, and its two sizing functions.
struct GridTrackForTest {
    bool isGutter = false;
    bool isCollapsed = false;
    CompactLength min;
    CompactLength max;
};

// `initialize_grid_tracks`. Returns how many tracks it produced, writing up to
// `cap` of them.
int GridInitTracksForTest(const Style& style, AbsoluteAxis axis,
                          uint16_t negativeImplicit, uint16_t explicitCount,
                          uint16_t positiveImplicit, GridTrackForTest* out,
                          int cap);

// Where the placement algorithm put one item, in OriginZero coordinates.
struct GridPlacementForTest {
    int16_t columnStart = 0;
    int16_t columnEnd = 0;
    int16_t rowStart = 0;
    int16_t rowEnd = 0;
};

// `place_grid_items` over `parent`'s children. Returns how many items were
// placed; `outColCounts` and `outRowCounts` take three entries each.
int GridPlaceForTest(TaffyTree* tree, NodeId parent, uint16_t explicitColCount,
                     uint16_t explicitRowCount, GridAutoFlow flow,
                     GridPlacementForTest* out, int cap, uint16_t* outColCounts,
                     uint16_t* outRowCounts);

// ─── scrollable overflow — taffy/src/compute/common/scrollable_overflow.rs

// The rectangle a node contributes to its parent's scrollable overflow.
// `location` is the border box measured from the parent's scroll origin.
// A scroll container (Hidden or Scroll on either axis) clips both axes.
// Clip clips only its own axis. Visible propagates the child's rect.
// Layout or paint containment contributes only the border box.
// A box wholly before the scroll origin is dropped only when the parent
// is itself a scroll container.
RectF ComputeScrollableOverflowContribution(PointF location, SizeF size,
                                            RectF scrollableOverflowRect,
                                            PointOverflow overflow,
                                            Contain contain,
                                            bool parentIsScrollContainer);

} // namespace taffy

#include "taffy/flex_balance.h"

#endif // GPUI_TAFFY_COMPUTE_H_
