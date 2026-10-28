/* The CSS Grid layout algorithm — all of taffy/src/compute/grid, a partial
 * implementation of CSS Grid Level 1: https://www.w3.org/TR/css-grid-1
 *
 * Rust splits this over `mod.rs`, `alignment.rs`, `explicit_grid.rs`,
 * `implicit_grid.rs`, `placement.rs`, `track_sizing.rs` and the `types`
 * module. They are one file here, in that order, because every one of them is
 * private to the algorithm and none of them is named from outside it.
 *
 * Deviations from the Rust:
 *   - `detailed_layout_info` is not produced. It exists for consumers that
 *     want the computed track sizes back out; nothing in this tree reads it.
 *     A private track-position snapshot is stored on the node so the
 *     out-of-flow pass can resolve a grid area. That is not the public API.
 *   - A line named `<area>-start` or `<area>-end` is recognized by splitting
 *     that suffix off the existing string. Area edges live in their own map,
 *     so the joined name is never allocated.
 *   - Rust's closures for "which tracks are affected" and "what is a track's
 *     limit" are C++ lambdas passed to function templates, which compile away
 *     the same way.
 */

#include "taffy/compute.h"
#include "taffy/sizing_keyword.h"

namespace taffy {
namespace {

// ─── types/grid_track_counts.rs ──────────────────────────────────────────
//
// Taffy uses two coordinate systems for grid *tracks*, both covering the whole
// implicit grid:
//
//   CellOccupancyMatrix indices: 0 is the leftmost track of the implicit grid.
//   GridTrackVec indices: the vector stores lines and tracks together, so even
//   indices are lines and odd indices are tracks. Index 1 is the leftmost
//   track, index 0 the leftmost line.

// The number of tracks in a dimension, split by implicit and explicit.
struct TrackCounts {
    uint16_t negativeImplicit = 0;
    uint16_t explicitCount = 0;
    uint16_t positiveImplicit = 0;

    static TrackCounts FromRaw(uint16_t neg, uint16_t exp, uint16_t pos) {
        return {neg, exp, pos};
    }
    int Len() const {
        return (int)negativeImplicit + (int)explicitCount +
               (int)positiveImplicit;
    }
    OriginZeroLine ImplicitStartLine() const {
        return OriginZeroLine{(int16_t)(-(int16_t)negativeImplicit)};
    }
    OriginZeroLine ImplicitEndLine() const {
        return OriginZeroLine{(int16_t)(explicitCount + positiveImplicit)};
    }
    // The track immediately after a line, as a matrix index.
    int16_t OzLineToNextTrack(OriginZeroLine l) const {
        return (int16_t)(l.v + (int16_t)negativeImplicit);
    }
    // The line immediately before a track, in OriginZero coordinates.
    OriginZeroLine TrackToPrevOzLine(uint16_t index) const {
        return OriginZeroLine{
            (int16_t)((int16_t)index - (int16_t)negativeImplicit)};
    }
};

// The GridTrackVec index of an OriginZero line. Rust panics when the line is
// out of range; the fallible form below is what absolutely positioned items
// use.
int IntoTrackVecIndex(OriginZeroLine l, TrackCounts counts) {
    return 2 * (int)(l.v + (int16_t)counts.negativeImplicit);
}

bool TryIntoTrackVecIndex(OriginZeroLine l, TrackCounts counts, int* out) {
    if (l.v < -(int16_t)counts.negativeImplicit) {
        return false;
    }
    if (l.v > (int16_t)(counts.explicitCount + counts.positiveImplicit)) {
        return false;
    }
    *out = 2 * (int)(l.v + (int16_t)counts.negativeImplicit);
    return true;
}

// ─── types/grid_track.rs ─────────────────────────────────────────────────

// Rust's `Line<u16>`: an item's placement as GridTrackVec indices.
struct LineU16 {
    uint16_t start = 0;
    uint16_t end = 0;
};

enum class GridTrackKind : uint8_t {
    Track,
    Gutter
};

// One grid track (row/column). Gutters are sized like tracks, so they are the
// same struct.
struct GridTrack {
    GridTrackKind kind = GridTrackKind::Track;
    // A collapsed track is treated as if it did not exist for sizing.
    bool isCollapsed = false;
    MinTrackSizingFunction minTrackSizingFunction;
    MaxTrackSizingFunction maxTrackSizingFunction;
    // The distance from the start of the grid container.
    float offset = 0.0f;
    float baseSize = 0.0f;
    // Scratch; can be infinity.
    float growthLimit = 0.0f;
    // Scratch: an extra amount added to the estimate of the available space in
    // the opposite axis when content-sizing items.
    float contentAlignmentAdjustment = 0.0f;
    // Scratch, while distributing space.
    float itemIncurredIncrease = 0.0f;
    float baseSizePlannedIncrease = 0.0f;
    float growthLimitPlannedIncrease = 0.0f;
    // https://www.w3.org/TR/css3-grid-layout/#infinitely-growable
    bool infinitelyGrowable = false;
    // The resolved fit-content() limit. Infinity when this track is not
    // fit-content(), or the limit is a percentage or calc() and the container
    // size is indefinite. Filled once per track-sizing pass.
    float fitContentLimit = INFINITY;

    static GridTrack New(MinTrackSizingFunction mn, MaxTrackSizingFunction mx) {
        GridTrack t;
        t.kind = GridTrackKind::Track;
        t.minTrackSizingFunction = mn;
        t.maxTrackSizingFunction = mx;
        return t;
    }
    static GridTrack Gutter(LengthPercentage size) {
        GridTrack t;
        t.kind = GridTrackKind::Gutter;
        t.minTrackSizingFunction = MinTrackSizingFunction::From(size);
        t.maxTrackSizingFunction = MaxTrackSizingFunction::From(size);
        return t;
    }
    void Collapse() {
        isCollapsed = true;
        minTrackSizingFunction = MinTrackSizingFunction::Zero();
        maxTrackSizingFunction = MaxTrackSizingFunction::Zero();
        fitContentLimit = INFINITY;
    }
    bool IsFlexible() const { return maxTrackSizingFunction.IsFr(); }
    bool UsesPercentage() const {
        return minTrackSizingFunction.UsesPercentage() || maxTrackSizingFunction
                                                              .UsesPercentage();
    }
    bool HasIntrinsicSizingFunction() const {
        return minTrackSizingFunction.raw.IsIntrinsic() ||
               maxTrackSizingFunction.IsIntrinsic();
    }
    void ResolveFitContentLimit(Optf axisInnerNodeSize, CalcResolver calc) {
        fitContentLimit = UnwrapOr(
            maxTrackSizingFunction.FitContentLimit(axisInnerNodeSize, calc),
            INFINITY);
    }
    float FitContentLimit() const { return fitContentLimit; }
    float FitContentLimitedGrowthLimit() const {
        return F32Min(growthLimit, fitContentLimit);
    }
    float FlexFactor() const {
        return maxTrackSizingFunction.IsFr()
                   ? maxTrackSizingFunction.raw.Value()
                   : 0.0f;
    }
};

// ─── types/cell_occupancy.rs ─────────────────────────────────────────────

enum class CellOccupancyState : uint8_t {
    Unoccupied,
    DefinitelyPlaced,
    AutoPlaced
};

// The furthest primary-axis end line of an auto-placed item that started in
// this secondary-axis track. Unset until such an item is placed.
struct SparseCursor {
    bool set = false;
    int16_t line = 0;
};

// A dynamically sized matrix tracking the occupancy of each cell during
// auto-placement, plus how many tracks are implicit and explicit.
// Sparse cursors are stored per secondary track, separate from occupancy:
// an item that spans into a later track does not move that track's cursor.
struct CellOccupancyMatrix {
    Vec<uint8_t> inner;
    Vec<SparseCursor> rowCursor;
    Vec<SparseCursor> colCursor;
    int nRows = 0;
    int nCols = 0;
    TrackCounts columns;
    TrackCounts rows;

    static void ResetCursors(Vec<SparseCursor>& cursors, int count) {
        VecReset(cursors);
        if (count <= 0) {
            return;
        }
        SparseCursor* p = VecAppendBlanks(cursors, count);
        if (p) {
            memset(p, 0, (size_t)count * sizeof(SparseCursor));
        }
    }
    // Negative growth inserts empty tracks at the front. Cursor values are
    // origin-zero lines, so they move with their track and do not change.
    static void ShiftCursors(Vec<SparseCursor>& cursors, int oldCount,
                             int reqNeg, int reqPos) {
        if (reqNeg == 0 && reqPos == 0) {
            return;
        }
        int newCount = oldCount + reqNeg + reqPos;
        Vec<SparseCursor> next;
        SparseCursor* p =
            newCount > 0 ? VecAppendBlanks(next, newCount) : nullptr;
        if (newCount > 0 && !p) {
            return;
        }
        if (p) {
            memset(p, 0, (size_t)newCount * sizeof(SparseCursor));
            if (oldCount > 0 && cursors.els) {
                memcpy(p + reqNeg, cursors.els,
                       (size_t)oldCount * sizeof(SparseCursor));
            }
        }
        VecReset(cursors);
        cursors = next;
        next.els = nullptr;
        next.len = 0;
        next.cap = 0;
    }

    void Init(TrackCounts cols, TrackCounts rws) {
        columns = cols;
        rows = rws;
        nRows = rws.Len();
        nCols = cols.Len();
        VecReset(inner);
        int n = nRows * nCols;
        if (n > 0) {
            uint8_t* p = VecAppendBlanks(inner, n);
            if (p) {
                memset(p, 0, (size_t)n);
            }
        }
        ResetCursors(rowCursor, nRows);
        ResetCursors(colCursor, nCols);
    }
    void Free() {
        VecReset(inner);
        VecReset(rowCursor);
        VecReset(colCursor);
    }

    CellOccupancyState Get(int row, int col) const {
        if (row < 0 || row >= nRows || col < 0 || col >= nCols) {
            return CellOccupancyState::Unoccupied;
        }
        return (CellOccupancyState)inner[row * nCols + col];
    }
    void Set(int row, int col, CellOccupancyState v) {
        if (row < 0 || row >= nRows || col < 0 || col >= nCols) {
            return;
        }
        inner[row * nCols + col] = (uint8_t)v;
    }
    const TrackCounts& Counts(AbsoluteAxis a) const {
        return a == AbsoluteAxis::Horizontal ? columns : rows;
    }

    bool IsAreaInRange(AbsoluteAxis primaryAxis, int primaryStart,
                       int primaryEnd, int secondaryStart,
                       int secondaryEnd) const {
        if (primaryStart < 0 || primaryEnd > Counts(primaryAxis).Len()) {
            return false;
        }
        if (secondaryStart < 0 || secondaryEnd > Counts(OtherAxis(primaryAxis))
                                                     .Len()) {
            return false;
        }
        return true;
    }

    // Grow the matrix (in any of the four directions) so the given ranges fit.
    void ExpandToFitRange(int rowStart, int rowEnd, int colStart, int colEnd) {
        int reqNegRows = rowStart < 0 ? -rowStart : 0;
        int reqPosRows = rowEnd - rows.Len() > 0 ? rowEnd - rows.Len() : 0;
        int reqNegCols = colStart < 0 ? -colStart : 0;
        int reqPosCols =
            colEnd - columns.Len() > 0 ? colEnd - columns.Len() : 0;

        int oldRowCount = nRows;
        int oldColCount = nCols;
        int newRowCount = oldRowCount + reqNegRows + reqPosRows;
        int newColCount = oldColCount + reqNegCols + reqPosCols;

        Vec<uint8_t> data;
        uint8_t* p = VecAppendBlanks(data, newRowCount * newColCount);
        if (!p) {
            return;
        }
        memset(p, 0, (size_t)newRowCount * (size_t)newColCount);
        for (int row = 0; row < oldRowCount; row++) {
            for (int col = 0; col < oldColCount; col++) {
                p[(row + reqNegRows) * newColCount + (col + reqNegCols)] =
                    inner[row * nCols + col];
            }
        }
        VecReset(inner);
        inner = data;
        data.els = nullptr;
        data.len = 0;
        data.cap = 0;

        nRows = newRowCount;
        nCols = newColCount;
        rows.negativeImplicit = (uint16_t)(rows.negativeImplicit + reqNegRows);
        rows.positiveImplicit = (uint16_t)(rows.positiveImplicit + reqPosRows);
        columns.negativeImplicit =
            (uint16_t)(columns.negativeImplicit + reqNegCols);
        columns.positiveImplicit =
            (uint16_t)(columns.positiveImplicit + reqPosCols);
        ShiftCursors(rowCursor, oldRowCount, reqNegRows, reqPosRows);
        ShiftCursors(colCursor, oldColCount, reqNegCols, reqPosCols);
    }

    void AdvanceAutoPlacementCursor(AbsoluteAxis primaryAxis,
                                    OriginZeroLine secondaryStart,
                                    OriginZeroLine primaryEnd) {
        int trackIndex = Counts(OtherAxis(primaryAxis))
                             .OzLineToNextTrack(secondaryStart);
        Vec<SparseCursor>& tracks =
            primaryAxis == AbsoluteAxis::Horizontal ? rowCursor : colCursor;
        if (trackIndex < 0 || trackIndex >= len(tracks)) {
            return;
        }
        SparseCursor& cursor = tracks.els[trackIndex];
        if (!cursor.set || primaryEnd.v > cursor.line) {
            cursor.set = true;
            cursor.line = primaryEnd.v;
        }
    }

    // The sparse cursor for items whose secondary span starts on this line.
    OptOriginZeroLine AutoPlacementCursor(AbsoluteAxis trackType,
                                          OriginZeroLine startAt) const {
        int idx = Counts(OtherAxis(trackType)).OzLineToNextTrack(startAt);
        const Vec<SparseCursor>& tracks =
            trackType == AbsoluteAxis::Horizontal ? rowCursor : colCursor;
        if (idx < 0 || idx >= len(tracks) || !tracks.els[idx].set) {
            return OptOriginZeroLine();
        }
        return OptOriginZeroLine(OriginZeroLine{tracks.els[idx].line});
    }

    void MarkAreaAs(AbsoluteAxis primaryAxis, LineOzl primarySpan,
                    LineOzl secondarySpan, CellOccupancyState value) {
        LineOzl rowSpan = primaryAxis == AbsoluteAxis::Horizontal
                              ? secondarySpan
                              : primarySpan;
        LineOzl colSpan = primaryAxis == AbsoluteAxis::Horizontal
                              ? primarySpan
                              : secondarySpan;

        int colStart = columns.OzLineToNextTrack(colSpan.start);
        int colEnd = columns.OzLineToNextTrack(colSpan.end);
        int rowStart = rows.OzLineToNextTrack(rowSpan.start);
        int rowEnd = rows.OzLineToNextTrack(rowSpan.end);

        if (!IsAreaInRange(AbsoluteAxis::Horizontal, colStart, colEnd, rowStart,
                           rowEnd)) {
            ExpandToFitRange(rowStart, rowEnd, colStart, colEnd);
            colStart = columns.OzLineToNextTrack(colSpan.start);
            colEnd = columns.OzLineToNextTrack(colSpan.end);
            rowStart = rows.OzLineToNextTrack(rowSpan.start);
            rowEnd = rows.OzLineToNextTrack(rowSpan.end);
        }

        if (value == CellOccupancyState::AutoPlaced &&
            primarySpan.start < primarySpan.end &&
            secondarySpan.start < secondarySpan.end) {
            AdvanceAutoPlacementCursor(primaryAxis, secondarySpan.start,
                                       primarySpan.end);
        }

        for (int x = rowStart; x < rowEnd; x++) {
            for (int y = colStart; y < colEnd; y++) {
                Set(x, y, value);
            }
        }
    }

    bool TrackAreaIsUnoccupied(AbsoluteAxis primaryAxis, int primaryStart,
                               int primaryEnd, int secondaryStart,
                               int secondaryEnd) const {
        int rowStart = primaryAxis == AbsoluteAxis::Horizontal ? secondaryStart
                                                               : primaryStart;
        int rowEnd =
            primaryAxis == AbsoluteAxis::Horizontal ? secondaryEnd : primaryEnd;
        int colStart = primaryAxis == AbsoluteAxis::Horizontal ? primaryStart
                                                               : secondaryStart;
        int colEnd =
            primaryAxis == AbsoluteAxis::Horizontal ? primaryEnd : secondaryEnd;
        // Out-of-bounds cells count as unoccupied.
        for (int x = rowStart; x < rowEnd; x++) {
            for (int y = colStart; y < colEnd; y++) {
                if (Get(x, y) != CellOccupancyState::Unoccupied) {
                    return false;
                }
            }
        }
        return true;
    }

    bool LineAreaIsUnoccupied(AbsoluteAxis primaryAxis, LineOzl primarySpan,
                              LineOzl secondarySpan) const {
        const TrackCounts& pc = Counts(primaryAxis);
        const TrackCounts& sc = Counts(OtherAxis(primaryAxis));
        return TrackAreaIsUnoccupied(primaryAxis,
                                     pc.OzLineToNextTrack(primarySpan.start),
                                     pc.OzLineToNextTrack(primarySpan.end),
                                     sc.OzLineToNextTrack(secondarySpan.start),
                                     sc.OzLineToNextTrack(secondarySpan.end));
    }

    // The next auto-placement cursor position past every occupied interval
    // colliding with this area. Rust 0.13 stores sparse intervals directly;
    // this POD port keeps its compact flat matrix and derives the same extent
    // while scanning, preserving the placement jump without another owning
    // container shape.
    OptOriginZeroLine LineAreaCollisionJump(AbsoluteAxis primaryAxis,
                                            LineOzl primarySpan,
                                            LineOzl secondarySpan) const {
        const TrackCounts& pc = Counts(primaryAxis);
        const TrackCounts& sc = Counts(OtherAxis(primaryAxis));
        int primaryStart = pc.OzLineToNextTrack(primarySpan.start);
        int primaryEnd = pc.OzLineToNextTrack(primarySpan.end);
        int secondaryStart = sc.OzLineToNextTrack(secondarySpan.start);
        int secondaryEnd = sc.OzLineToNextTrack(secondarySpan.end);
        int primaryLen = pc.Len();
        int secondaryLen = sc.Len();
        primaryStart = primaryStart < 0 ? 0 : primaryStart;
        primaryEnd = primaryEnd > primaryLen ? primaryLen : primaryEnd;
        secondaryStart = secondaryStart < 0 ? 0 : secondaryStart;
        secondaryEnd =
            secondaryEnd > secondaryLen ? secondaryLen : secondaryEnd;

        bool found = false;
        int best = 0;
        for (int secondary = secondaryStart; secondary < secondaryEnd;
             secondary++) {
            for (int primary = primaryStart; primary < primaryEnd; primary++) {
                int row = primaryAxis == AbsoluteAxis::Horizontal ? secondary
                                                                  : primary;
                int col = primaryAxis == AbsoluteAxis::Horizontal ? primary
                                                                  : secondary;
                if (Get(row, col) == CellOccupancyState::Unoccupied) {
                    continue;
                }

                // The end of the last overlapping run. Every search position
                // before that extent also collides.
                int extent = primary;
                while (extent + 1 < primaryLen) {
                    int r = primaryAxis == AbsoluteAxis::Horizontal
                                ? secondary
                                : extent + 1;
                    int c = primaryAxis == AbsoluteAxis::Horizontal ? extent + 1
                                                                    : secondary;
                    if (Get(r, c) == CellOccupancyState::Unoccupied) {
                        break;
                    }
                    extent++;
                }
                best = !found || extent > best ? extent : best;
                found = true;
            }
        }
        if (!found) {
            return OptOriginZeroLine();
        }
        OriginZeroLine line = pc.TrackToPrevOzLine((uint16_t)best);
        int32_t next = (int32_t)line.v + 1;
        next = next < INT16_MIN   ? INT16_MIN
               : next > INT16_MAX ? INT16_MAX
                                  : next;
        return OptOriginZeroLine(OriginZeroLine{(int16_t)next});
    }

    bool RowIsOccupied(int rowIndex) const {
        if (rowIndex < 0 || rowIndex >= nRows) {
            return false;
        }
        for (int c = 0; c < nCols; c++) {
            if (Get(rowIndex, c) != CellOccupancyState::Unoccupied) {
                return true;
            }
        }
        return false;
    }
    bool ColumnIsOccupied(int columnIndex) const {
        if (columnIndex < 0 || columnIndex >= nCols) {
            return false;
        }
        for (int r = 0; r < nRows; r++) {
            if (Get(r, columnIndex) != CellOccupancyState::Unoccupied) {
                return true;
            }
        }
        return false;
    }
};

// ─── types/grid_item.rs ──────────────────────────────────────────────────

struct GridItem {
    NodeId node;
    // The index of the item among the children. Track sizing re-sorts the
    // list; this puts it back for final positioning.
    uint16_t sourceOrder = 0;

    // The definite placement the placement algorithm resolved, in OriginZero
    // coordinates.
    LineOzl row;
    LineOzl column;

    bool isCompressibleReplaced = false;
    PointOverflow overflow;
    BoxSizing boxSizing = BoxSizing::BorderBox;
    SizeDim size;
    SizeDim minSize;
    SizeDim maxSize;
    Optf aspectRatio = None();
    RectLp padding;
    RectLp border;
    RectLpa margin;
    AlignSelf alignSelf;
    AlignSelf justifySelf;
    Optf baseline = None();
    // Acts like an extra top margin, for baseline alignment.
    float baselineShim = 0.0f;

    // The same placement as GridTrackVec indices.
    LineU16 rowIndexes;
    LineU16 columnIndexes;

    bool crossesFlexibleRow = false;
    bool crossesFlexibleColumn = false;
    bool crossesIntrinsicRow = false;
    bool crossesIntrinsicColumn = false;

    // Caches, valid for a single run of the track-sizing algorithm.
    SizeFOpt gridAreaSizeCache = SizeFOptNone();
    bool hasGridAreaSizeCache = false;
    SizeFOpt minContentContributionCache = SizeFOptNone();
    SizeFOpt minimumContributionCache = SizeFOptNone();
    SizeFOpt maxContentContributionCache = SizeFOptNone();

    // Final position and height, for the container's baseline.
    float yPosition = 0.0f;
    float height = 0.0f;
    // Bubbled out-of-flow candidates, translated by this item's location.
    // A range into the pass's pool: GridItem lives in a Vec.
    int oofStart = 0;
    int oofCount = 0;

    // css-align-3 §9.5: an auto margin on the block axis opts the item
    // out of baseline alignment.
    bool HasAutoBlockMargin() const {
        return margin.top.IsAuto() || margin.bottom.IsAuto();
    }
    // css-grid-1 §9.2: a percentage block size on an intrinsic or flexible
    // row depends on the row size that baseline alignment itself affects,
    // so the item uses its fallback alignment instead.
    bool HasCyclicBlockSizeDependency() const {
        return size.height.raw.UsesPercentage() &&
               (crossesIntrinsicRow || crossesFlexibleRow);
    }
    bool ParticipatesInBaselineAlignment() const {
        return alignSelf.keyword == AlignItemsKeyword::Baseline &&
               !HasAutoBlockMargin() && !HasCyclicBlockSizeDependency();
    }

    LineOzl Placement(AbstractAxis axis) const {
        return axis == AbstractAxis::Block ? row : column;
    }
    LineU16 PlacementIndexes(AbstractAxis axis) const {
        return axis == AbstractAxis::Block ? rowIndexes : columnIndexes;
    }
    // The GridTrackVec range covering the tracks and lines this item spans,
    // excluding the lines that bound it.
    int TrackRangeStart(AbstractAxis axis) const {
        return (int)PlacementIndexes(axis).start + 1;
    }
    int TrackRangeEnd(AbstractAxis axis) const {
        return (int)PlacementIndexes(axis).end;
    }
    // True when any track this item spans satisfies the predicate. An item
    // whose tracks cannot receive a contribution is not measured for it.
    template <typename Pred>
    bool SpansTrackMatching(AbstractAxis axis, const GridTrack* tracks,
                            Pred pred) const {
        int from = TrackRangeStart(axis);
        int end = TrackRangeEnd(axis);
        for (int k = from; k < end; k++) {
            if (pred(tracks[k])) {
                return true;
            }
        }
        return false;
    }
    uint16_t Span(AbstractAxis axis) const { return Placement(axis).Span(); }
    bool CrossesFlexibleTrack(AbstractAxis axis) const {
        return axis == AbstractAxis::Inline ? crossesFlexibleColumn
                                            : crossesFlexibleRow;
    }
    bool CrossesIntrinsicTrack(AbstractAxis axis) const {
        return axis == AbstractAxis::Inline ? crossesIntrinsicColumn
                                            : crossesIntrinsicRow;
    }
};

// ─── stable sort ─────────────────────────────────────────────────────────
//
// Rust's `sort_by` and `sort_by_key` are stable, and grid placement depends on
// that: two items with the same sort key have to stay in document order.
//
// This is a bottom-up merge sort over insertion-sorted runs, which is the
// shape of Rust's own `slice::sort`. It began as a plain insertion sort, on
// the reasoning that a grid's item list is short — which is true of a grid
// someone wrote by hand and false of the ones the benchmarks build. A 316x316
// grid has 99,856 items, and sorting them by hand took 94 seconds of the
// benchmark's 95.

template <typename T, typename Less>
static void InsertionSortRange(T* items, int lo, int hi, Less less) {
    for (int i = lo + 1; i < hi; i++) {
        T key = items[i];
        int j = i - 1;
        while (j >= lo && less(key, items[j])) {
            items[j + 1] = items[j];
            j--;
        }
        items[j + 1] = key;
    }
}

// `!less(right, left)` rather than `less(left, right)` is what keeps equal
// elements in their original order.
template <typename T, typename Less>
static void MergeRuns(const T* src, T* dst, int lo, int mid, int hi,
                      Less less) {
    int i = lo;
    int j = mid;
    int k = lo;
    while (i < mid && j < hi) {
        if (!less(src[j], src[i])) {
            dst[k++] = src[i++];
        } else {
            dst[k++] = src[j++];
        }
    }
    while (i < mid) {
        dst[k++] = src[i++];
    }
    while (j < hi) {
        dst[k++] = src[j++];
    }
}

template <typename T, typename Less>
void StableSort(T* items, int n, Less less) {
    if (n < 2) {
        return;
    }
    // Short runs are insertion sorted, then merged pairwise. Below the
    // threshold there is nothing to merge.
    const int kRun = 32;
    for (int lo = 0; lo < n; lo += kRun) {
        int hi = lo + kRun < n ? lo + kRun : n;
        InsertionSortRange(items, lo, hi, less);
    }
    if (n <= kRun) {
        return;
    }

    T* scratch = (T*)base::Alloc(nullptr, n * (int)sizeof(T));
    if (!scratch) {
        // Out of memory for the scratch half. The insertion sort still sorts,
        // just slowly, and a sorted list is what the caller needs.
        InsertionSortRange(items, 0, n, less);
        return;
    }

    T* src = items;
    T* dst = scratch;
    for (int width = kRun; width < n; width *= 2) {
        for (int lo = 0; lo < n; lo += 2 * width) {
            int mid = lo + width < n ? lo + width : n;
            int hi = lo + 2 * width < n ? lo + 2 * width : n;
            MergeRuns(src, dst, lo, mid, hi, less);
        }
        T* swap = src;
        src = dst;
        dst = swap;
    }
    if (src != items) {
        memcpy((void*)items, (const void*)src, (size_t)n * sizeof(T));
    }
    base::Free(nullptr, (void*)scratch);
}

// ─── types/named.rs ──────────────────────────────────────────────────────
//
// Rust builds the strings "{name}-start" and "{name}-end" to key its maps.
// Here the suffix is a field, so a lookup composes the comparison instead of
// the string.

struct LineNameEntry {
    Str name;
    Vec<uint32_t> lines;
};

// The first line that starts and ends a named area on each axis. Zero means
// that edge has no line. An explicit `<area>-start` / `<area>-end` line keeps
// the smaller line number.
struct AreaLines {
    Str name;
    uint32_t rowStart = 0;
    uint32_t rowEnd = 0;
    uint32_t colStart = 0;
    uint32_t colEnd = 0;

    uint32_t Edge(GridAreaAxis axis, GridAreaEnd end) const {
        if (axis == GridAreaAxis::Row) {
            return end == GridAreaEnd::Start ? rowStart : rowEnd;
        }
        return end == GridAreaEnd::Start ? colStart : colEnd;
    }
    uint32_t& EdgeMut(GridAreaAxis axis, GridAreaEnd end) {
        if (axis == GridAreaAxis::Row) {
            return end == GridAreaEnd::Start ? rowStart : rowEnd;
        }
        return end == GridAreaEnd::Start ? colStart : colEnd;
    }
};

// `<area>-start` or `<area>-end`. `area` aliases `name`; it is not allocated.
bool SplitAreaEdgeName(Str name, Str* area, GridAreaEnd* end) {
    if (StrEndsWith(name, "-start") && len(name) > 6) {
        *area = Str(name.s, len(name) - 6);
        *end = GridAreaEnd::Start;
        return true;
    }
    if (StrEndsWith(name, "-end") && len(name) > 4) {
        *area = Str(name.s, len(name) - 4);
        *end = GridAreaEnd::End;
        return true;
    }
    return false;
}

// Resolves named grid lines and areas into line numbers.
struct NamedLineResolver {
    Vec<LineNameEntry> rowLines;
    Vec<LineNameEntry> columnLines;
    Vec<AreaLines> areaLines;
    uint16_t areaColumnCount = 0;
    uint16_t areaRowCount = 0;
    uint16_t explicitColumnCount = 0;
    uint16_t explicitRowCount = 0;

    void Free() {
        for (int i = 0; i < len(rowLines); i++) {
            VecReset(rowLines[i].lines);
        }
        for (int i = 0; i < len(columnLines); i++) {
            VecReset(columnLines[i].lines);
        }
        VecReset(rowLines);
        VecReset(columnLines);
        VecReset(areaLines);
    }

    AreaLines* FindArea(Str name) {
        for (int i = 0; i < len(areaLines); i++) {
            if (base::StrEq(areaLines[i].name, name)) {
                return &areaLines[i];
            }
        }
        return nullptr;
    }
    const AreaLines* FindArea(Str name) const {
        for (int i = 0; i < len(areaLines); i++) {
            if (base::StrEq(areaLines[i].name, name)) {
                return &areaLines[i];
            }
        }
        return nullptr;
    }
    uint32_t AreaEdge(Str name, GridAreaAxis axis, GridAreaEnd end) const {
        const AreaLines* area = FindArea(name);
        return area ? area->Edge(axis, end) : 0;
    }

    // Record `value` under `name`. A `*-start` / `*-end` name also updates
    // that area edge to the first such line. The first time the name is seen,
    // an area edge that already existed is recorded in front of `value`;
    // Init sorts and dedups afterwards.
    void Upsert(Vec<LineNameEntry>* map, GridAreaAxis axis, Str name,
                uint32_t value) {
        uint32_t priorEdge = 0;
        Str areaName;
        GridAreaEnd edgeEnd;
        if (SplitAreaEdgeName(name, &areaName, &edgeEnd)) {
            AreaLines* area = FindArea(areaName);
            if (!area) {
                AreaLines created;
                created.name = areaName;
                VecAppend(areaLines, created);
                area = &areaLines[len(areaLines) - 1];
            }
            uint32_t& edge = area->EdgeMut(axis, edgeEnd);
            priorEdge = edge;
            edge = priorEdge == 0 || value < priorEdge ? value : priorEdge;
        }

        for (int i = 0; i < map->len; i++) {
            LineNameEntry& e = (*map)[i];
            if (base::StrEq(e.name, name)) {
                for (int k = 0; k < e.lines.len; k++) {
                    if (e.lines[k] == value) {
                        return;
                    }
                }
                VecAppend(e.lines, value);
                return;
            }
        }
        LineNameEntry e;
        e.name = name;
        if (priorEdge != 0) {
            VecAppend(e.lines, priorEdge);
        }
        if (priorEdge != value) {
            VecAppend(e.lines, value);
        }
        VecAppend(*map, e);
    }

    static const Vec<uint32_t>* Find(const Vec<LineNameEntry>& map, Str name) {
        for (int i = 0; i < len(map); i++) {
            if (base::StrEq(map[i].name, name)) {
                return &map[i].lines;
            }
        }
        return nullptr;
    }

    static void SortUnique(Vec<uint32_t>& lines) {
        for (int i = 1; i < lines.len; i++) {
            uint32_t v = lines[i];
            int j = i;
            while (j > 0 && lines[j - 1] > v) {
                lines[j] = lines[j - 1];
                j--;
            }
            lines[j] = v;
        }
        int w = 0;
        for (int i = 0; i < lines.len; i++) {
            if (w == 0 || lines[w - 1] != lines[i]) {
                lines[w++] = lines[i];
            }
        }
        lines.len = w;
    }

    void Init(const Style& style, uint16_t columnAutoRepetitions,
              uint16_t rowAutoRepetitions);
    LinePlain ResolveLineNames(LinePlacement line, GridAreaAxis axis) const;
    LinePlain ResolveRowNames(LinePlacement line) const {
        return ResolveLineNames(line, GridAreaAxis::Row);
    }
    LinePlain ResolveColumnNames(LinePlacement line) const {
        return ResolveLineNames(line, GridAreaAxis::Column);
    }
    // A bare ident (index 0) matches the area edge before a line of the same
    // name. A positive index counts origin-zero lines forward from the
    // explicit start; a negative index counts back from the explicit end.
    GridLine FindNamedLine(Str name, int16_t idx, GridAreaAxis axis,
                           GridAreaEnd end) const;
    OriginZeroLine LastExplicitLine(GridAreaAxis axis) const;
    // `lines` are sorted 1-based explicit line numbers. `n` counts from 1.
    // The search bounds are origin-zero and may sit outside an i16: a span
    // steps one line off a clamped grid line in i32.
    OriginZeroLine NthLineForwards(const uint32_t* lines, int count,
                                   int32_t firstOz, int32_t n,
                                   GridAreaAxis axis) const;
    OriginZeroLine NthLineBackwards(const uint32_t* lines, int count,
                                    int32_t lastOz, int32_t n,
                                    GridAreaAxis axis) const;
    // Lines literally named `name`, or the single area edge when `name` is
    // `<area>-start` / `<area>-end` and no line was given that name. `single`
    // holds that one edge; the returned pointer may alias it.
    int CollectLines(Str name, GridAreaAxis axis, const uint32_t** data,
                     uint32_t* single) const;
};

// Named-line maps store 1-based line numbers. Origin-zero is that minus one.
static int32_t OzOfStored(uint32_t line) {
    return (int32_t)line - 1;
}

static OriginZeroLine ClampedOz(int32_t line) {
    if (line < (int32_t)kMinOzLine) {
        line = kMinOzLine;
    } else if (line > (int32_t)kMaxOzLine) {
        line = kMaxOzLine;
    }
    return OriginZeroLine{(int16_t)line};
}

// Inverse of IntoOriginZeroLine. A clamped origin-zero line and an explicit
// count within the track limit stay inside i16 (-20001..=10001).
static GridLine IntoGridLine(OriginZeroLine line, uint16_t explicitTrackCount) {
    int32_t explicitLineCount = (int32_t)explicitTrackCount + 1;
    int32_t value =
        line.v >= 0 ? (int32_t)line.v + 1 : (int32_t)line.v - explicitLineCount;
    return GridLine{(int16_t)value};
}

void NamedLineResolver::Init(const Style& style, uint16_t columnAutoRepetitions,
                             uint16_t rowAutoRepetitions) {
    areaColumnCount = style.gridTemplateAreas.columnCount;
    areaRowCount = style.gridTemplateAreas.rowCount;
    for (int i = 0; i < style.gridTemplateAreas.areas.len; i++) {
        const GridTemplateArea& area = style.gridTemplateAreas.areas[i];
        AreaLines lines;
        lines.name = area.name;
        lines.rowStart = area.rowStart;
        lines.rowEnd = area.rowEnd;
        lines.colStart = area.columnStart;
        lines.colEnd = area.columnEnd;
        VecAppend(areaLines, lines);
    }

    // The line names between the tracks, walked alongside the template so a
    // repeat() contributes its names once per repetition.
    struct Axis {
        Slice<GridTemplateComponent> tracks;
        Slice<LineNameSet> names;
        uint16_t autoRepetitions;
        Vec<LineNameEntry>* map;
        GridAreaAxis axis;
    };
    Axis axes[2] = {{style.gridTemplateColumns, style.gridTemplateColumnNames,
                     columnAutoRepetitions, &columnLines, GridAreaAxis::Column},
                    {style.gridTemplateRows, style.gridTemplateRowNames,
                     rowAutoRepetitions, &rowLines, GridAreaAxis::Row}};

    for (const Axis& ax : axes) {
        uint32_t currentLine = 0;
        int trackIdx = 0;
        for (int i = 0; i < ax.names.len; i++) {
            currentLine += 1;
            const LineNameSet& set = ax.names[i];
            for (int k = 0; k < set.names.len; k++) {
                Upsert(ax.map, ax.axis, set.names[k], currentLine);
            }
            if (trackIdx >= ax.tracks.len) {
                continue;
            }
            const GridTemplateComponent& comp = ax.tracks[trackIdx];
            trackIdx++;
            if (!comp.isRepeat) {
                continue;
            }
            uint16_t repeatCount = comp.repeat.count.IsAuto()
                                       ? ax.autoRepetitions
                                       : comp.repeat.count.count;
            // Line name sets are positional: set i names the i-th line of
            // each repetition. Empty means every line is unnamed. Any other
            // length must be track count + 1. Rust panics on a mismatch;
            // this tree has no exceptions, so a mismatch contributes no
            // names and the tracks are still counted.
            uint32_t lineNameSetCount = (uint32_t)comp.repeat.lineNames.len;
            uint32_t linesPerRep = comp.repeat.TrackCount();
            bool namesOk =
                lineNameSetCount == 0 || lineNameSetCount == linesPerRep + 1;
            for (uint16_t r = 0; r < repeatCount; r++) {
                if (namesOk) {
                    for (int s = 0; s < comp.repeat.lineNames.len; s++) {
                        uint32_t line = currentLine + (uint32_t)s;
                        const LineNameSet& ls = comp.repeat.lineNames[s];
                        for (int k = 0; k < ls.names.len; k++) {
                            Upsert(ax.map, ax.axis, ls.names[k], line);
                        }
                    }
                }
                currentLine += linesPerRep;
                // Names past the track limit are never resolved.
                if (currentLine > kMaxGridTracks) {
                    break;
                }
            }
            // The last line of the repetition collapses with the next set.
            if (repeatCount > 0 && currentLine > 0) {
                currentLine -= 1;
            }
        }
    }

    // An area edge recorded in front of a later explicit line can be out of
    // order. Sort and dedup, matching the resolver's final pass.
    for (int i = 0; i < len(columnLines); i++) {
        SortUnique(columnLines[i].lines);
    }
    for (int i = 0; i < len(rowLines); i++) {
        SortUnique(rowLines[i].lines);
    }
}

int NamedLineResolver::CollectLines(Str name, GridAreaAxis axis,
                                    const uint32_t** data,
                                    uint32_t* single) const {
    const Vec<LineNameEntry>& lookup =
        axis == GridAreaAxis::Row ? rowLines : columnLines;
    const Vec<uint32_t>* lines = Find(lookup, name);
    if (lines && lines->len > 0) {
        *data = lines->els;
        return lines->len;
    }
    Str areaName;
    GridAreaEnd edge;
    if (SplitAreaEdgeName(name, &areaName, &edge)) {
        uint32_t value = AreaEdge(areaName, axis, edge);
        if (value != 0) {
            *single = value;
            *data = single;
            return 1;
        }
    }
    *data = nullptr;
    return 0;
}

OriginZeroLine NamedLineResolver::LastExplicitLine(GridAreaAxis axis) const {
    uint16_t tracks =
        axis == GridAreaAxis::Row ? explicitRowCount : explicitColumnCount;
    return OriginZeroLine{(int16_t)tracks};
}

OriginZeroLine NamedLineResolver::NthLineForwards(const uint32_t* lines,
                                                  int count, int32_t firstOz,
                                                  int32_t n,
                                                  GridAreaAxis axis) const {
    // Lines before the search start do not count. A negative bound is the
    // explicit start: the named-line index never walks into negative tracks.
    int32_t first = firstOz > 0 ? firstOz : 0;
    int start = 0;
    while (start < count && OzOfStored(lines[start]) < first) {
        start++;
    }
    int kept = count - start;
    int32_t oz;
    if (n >= 1 && (n - 1) < kept) {
        oz = OzOfStored(lines[start + (int)(n - 1)]);
    } else {
        // Not enough explicit lines. Implicit lines after the explicit grid
        // count as matches.
        int32_t afterExplicit = (int32_t)LastExplicitLine(axis).v + 1;
        int32_t base = first > afterExplicit ? first : afterExplicit;
        oz = base + (n - kept) - 1;
    }
    return ClampedOz(oz);
}

OriginZeroLine NamedLineResolver::NthLineBackwards(const uint32_t* lines,
                                                   int count, int32_t lastOz,
                                                   int32_t n,
                                                   GridAreaAxis axis) const {
    int32_t lastExplicit = LastExplicitLine(axis).v;
    int32_t last = lastOz < lastExplicit ? lastOz : lastExplicit;
    int end = 0;
    while (end < count && OzOfStored(lines[end]) <= last) {
        end++;
    }
    int32_t oz;
    if (n >= 0 && (uint32_t)n <= (uint32_t)end) {
        oz = OzOfStored(lines[end - (int)n]);
    } else {
        // Not enough explicit lines. Implicit lines before the explicit grid
        // count as matches.
        int32_t base = last < -1 ? last : -1;
        oz = base - (n - end) + 1;
    }
    return ClampedOz(oz);
}

GridLine NamedLineResolver::FindNamedLine(Str name, int16_t idx,
                                          GridAreaAxis axis,
                                          GridAreaEnd end) const {
    uint16_t explicitCount =
        axis == GridAreaAxis::Row ? explicitRowCount : explicitColumnCount;
    // A custom-ident on its own matches `<ident>-start` / `<ident>-end`
    // before a line that was literally given that name. An indexed ident
    // does not.
    if (idx == 0) {
        uint32_t edge = AreaEdge(name, axis, end);
        if (edge != 0) {
            return IntoGridLine(ClampedOz(OzOfStored(edge)), explicitCount);
        }
    }

    uint32_t single = 0;
    const uint32_t* data = nullptr;
    int count = CollectLines(name, axis, &data, &single);
    OriginZeroLine line;
    if (idx >= 0) {
        int32_t n = idx < 1 ? 1 : (int32_t)idx;
        line = NthLineForwards(data, count, 0, n, axis);
    } else {
        line = NthLineBackwards(data, count, (int32_t)LastExplicitLine(axis).v,
                                -(int32_t)idx, axis);
    }
    return IntoGridLine(line, explicitCount);
}

LinePlain NamedLineResolver::ResolveLineNames(LinePlacement line,
                                              GridAreaAxis axis) const {
    GridPlacement start = line.start;
    GridPlacement end = line.end;

    if (start.kind == GridPlacementKind::NamedLine) {
        start = GridPlacement::FromLineIndex(
            FindNamedLine(start.name, start.line, axis, GridAreaEnd::Start).v);
    }
    if (end.kind == GridPlacementKind::NamedLine) {
        end = GridPlacement::FromLineIndex(
            FindNamedLine(end.name, end.line, axis, GridAreaEnd::End).v);
    }

    uint16_t explicitTrackCount =
        axis == GridAreaAxis::Row ? explicitRowCount : explicitColumnCount;

    // A definite non-zero line paired with a named span searches forward
    // from the line after the start, or backward from the line before the
    // end. A span does not take the bare-ident area-edge shortcut. A span
    // count of 0 is a span of 1.
    if (start.kind == GridPlacementKind::Line &&
        end.kind == GridPlacementKind::NamedSpan && start.line != 0) {
        int32_t after = (int32_t)IntoOriginZeroLine(GridLine{start.line},
                                                    explicitTrackCount)
                            .v +
                        1;
        uint32_t single = 0;
        const uint32_t* data = nullptr;
        int count = CollectLines(end.name, axis, &data, &single);
        int32_t n = end.span < 1 ? 1 : (int32_t)end.span;
        GridLine endLine = IntoGridLine(
            NthLineForwards(data, count, after, n, axis), explicitTrackCount);
        return {PlainPlacement::AtLine(start.line),
                PlainPlacement::AtLine(endLine.v)};
    }
    if (start.kind == GridPlacementKind::NamedSpan &&
        end.kind == GridPlacementKind::Line && end.line != 0) {
        int32_t before =
            (int32_t)IntoOriginZeroLine(GridLine{end.line}, explicitTrackCount)
                .v -
            1;
        uint32_t single = 0;
        const uint32_t* data = nullptr;
        int count = CollectLines(start.name, axis, &data, &single);
        int32_t n = start.span < 1 ? 1 : (int32_t)start.span;
        GridLine startLine = IntoGridLine(
            NthLineBackwards(data, count, before, n, axis), explicitTrackCount);
        return {PlainPlacement::AtLine(startLine.v),
                PlainPlacement::AtLine(end.line)};
    }

    auto plain = [](const GridPlacement& p) -> PlainPlacement {
        switch (p.kind) {
            case GridPlacementKind::Line:
                return PlainPlacement::AtLine(p.line);
            case GridPlacementKind::Span:
                return PlainPlacement::Spanning(p.span);
            case GridPlacementKind::NamedSpan:
                // A span for a named line with no matching line is a span of 1.
                return PlainPlacement::Spanning(1);
            default:
                return PlainPlacement::Auto();
        }
    };
    return {plain(start), plain(end)};
}

// ─── implicit_grid.rs ────────────────────────────────────────────────────
//
// Not required for spec compliance: an estimate of the grid size, used to
// pre-size the occupancy matrix and as a step in auto-placement.

struct MinMaxSpan {
    OriginZeroLine minLine;
    OriginZeroLine maxLine;
    uint16_t span = 0;
};

// A conservative estimate of the greatest and smallest grid lines one item
// uses, in OriginZero coordinates.
MinMaxSpan ChildMinLineMaxLineSpan(LinePlacement line,
                                   uint16_t explicitTrackCount) {
    // 8.3.1 Grid Placement Conflict Handling:
    //   A. Two lines with the start further end-ward than the end: swap them.
    //   B. Start equal to end: remove the end line.
    //   C. Two spans: remove the one from the end property.
    //   D. Only a span for a named line: replace it with a span of 1.
    //
    // Named lines are accounted for separately, so they are ignored here.
    LinePlain oz = line.IntoOriginZeroIgnoringNamed(explicitTrackCount);
    OriginZeroLine t1 = oz.start.Ozl();
    OriginZeroLine t2 = oz.end.Ozl();

    MinMaxSpan out;
    if (oz.start.IsLine() && oz.end.IsLine()) {
        out.minLine = (t1 == t2) ? t1 : (t1 < t2 ? t1 : t2);
        out.maxLine = (t1 == t2) ? t1 + (uint16_t)1 : (t1 > t2 ? t1 : t2);
    } else if (oz.start.IsLine() && oz.end.IsAuto()) {
        out.minLine = t1;
        out.maxLine = t1 + (uint16_t)1;
    } else if (oz.start.IsLine() && oz.end.IsSpan()) {
        out.minLine = t1;
        out.maxLine = t1 + oz.end.span;
    } else if (oz.start.IsAuto() && oz.end.IsLine()) {
        // A span of 1 ending at the definite line. Using the end line as the
        // start as well invents a positive implicit track on an empty grid.
        out.minLine = t2 - (uint16_t)1;
        out.maxLine = t2;
    } else if (oz.start.IsSpan() && oz.end.IsLine()) {
        out.minLine = t2 - oz.start.span;
        out.maxLine = t2;
    } else {
        // Only spans or autos: these are accounted for by the span below, so
        // the lines stay at zero and never affect the estimate.
        out.minLine = OriginZeroLine{0};
        out.maxLine = OriginZeroLine{0};
    }

    bool startIndefinite = oz.start.IsAuto() || oz.start.IsSpan();
    bool endIndefinite = oz.end.IsAuto() || oz.end.IsSpan();
    out.span = (startIndefinite && endIndefinite) ? oz.IndefiniteSpan() : 1;
    return out;
}

// One child's grid placement, as the estimate and the placement pass see it.
struct ChildPlacementStyles {
    LinePlacement column;
    LinePlacement row;
};

// Estimate the number of rows and columns in the implicit grid. The explicit
// and negative implicit counts are exact; the positive implicit count is a
// lower bound, since auto-placement can add to it.
void ComputeGridSizeEstimate(uint16_t explicitColCount,
                             uint16_t explicitRowCount,
                             const ChildPlacementStyles* children, int n,
                             TrackCounts* outCols, TrackCounts* outRows) {
    OriginZeroLine colMin{0};
    OriginZeroLine colMax{0};
    uint16_t colMaxSpan = 0;
    OriginZeroLine rowMin{0};
    OriginZeroLine rowMax{0};
    uint16_t rowMaxSpan = 0;

    for (int i = 0; i < n; i++) {
        MinMaxSpan colEst =
            ChildMinLineMaxLineSpan(children[i].column, explicitColCount);
        MinMaxSpan rowEst =
            ChildMinLineMaxLineSpan(children[i].row, explicitRowCount);

        if (colEst.minLine < colMin) {
            colMin = colEst.minLine;
        }
        if (colEst.maxLine > colMax) {
            colMax = colEst.maxLine;
        }
        if (colEst.span > colMaxSpan) {
            colMaxSpan = colEst.span;
        }
        if (rowEst.minLine < rowMin) {
            rowMin = rowEst.minLine;
        }
        if (rowEst.maxLine > rowMax) {
            rowMax = rowEst.maxLine;
        }
        if (rowEst.span > rowMaxSpan) {
            rowMaxSpan = rowEst.span;
        }
    }

    uint16_t negCols = ImpliedNegativeImplicitTracks(colMin);
    uint16_t posCols = ImpliedPositiveImplicitTracks(colMax, explicitColCount);
    uint16_t negRows = ImpliedNegativeImplicitTracks(rowMin);
    uint16_t posRows = ImpliedPositiveImplicitTracks(rowMax, explicitRowCount);

    // An item whose span does not fit in the estimate pushes the positive
    // count out to make room for it.
    if ((uint16_t)(negCols + explicitColCount + posCols) < colMaxSpan) {
        posCols = (uint16_t)(colMaxSpan - explicitColCount - negCols);
    }
    if ((uint16_t)(negRows + explicitRowCount + posRows) < rowMaxSpan) {
        posRows = (uint16_t)(rowMaxSpan - explicitRowCount - negRows);
    }

    *outCols = TrackCounts::FromRaw(negCols, explicitColCount, posCols);
    *outRows = TrackCounts::FromRaw(negRows, explicitRowCount, posRows);
}

// ─── explicit_grid.rs ────────────────────────────────────────────────────

// How many times an auto-repeat should repeat.
enum class AutoRepeatStrategy : uint8_t {
    // The container has a definite size or max size: the largest count that
    // does not overflow.
    MaxRepetitionsThatDoNotOverflow,
    // The container has a definite min size: the smallest count that meets it.
    MinRepetitionsThatDoOverflow
};

// Treat a track as its max sizing function when that is definite, or as its
// min otherwise, flooring the max by the min when both are definite.
float TrackDefiniteValue(TrackSizingFunction fn, Optf parentSize,
                         CalcResolver calc) {
    Optf maxSize = fn.max.DefiniteValue(parentSize, calc);
    Optf minSize = fn.min.DefiniteValue(parentSize, calc);
    if (IsSome(maxSize)) {
        return MaybeMax(maxSize, minSize);
    }
    return UnwrapOr(minSize, 0.0f);
}

struct ExplicitGridSize {
    uint16_t autoRepetitionCount = 0;
    uint16_t trackCount = 0;
};

ExplicitGridSize ComputeExplicitGridSizeInAxis(
    const Style& style, Optf autoFitContainerSize,
    AutoRepeatStrategy autoFitStrategy, CalcResolver calc, AbsoluteAxis axis) {
    Slice<GridTemplateComponent> templ = axis == AbsoluteAxis::Horizontal
                                             ? style.gridTemplateColumns
                                             : style.gridTemplateRows;
    if (templ.len == 0) {
        return {};
    }

    // A repetition with no tracks makes the whole definition invalid.
    for (int i = 0; i < templ.len; i++) {
        if (templ[i].isRepeat && templ[i].repeat.TrackCount() == 0) {
            return {};
        }
    }

    uint32_t nonAutoRepeatingTrackCount = 0;
    uint16_t autoRepetitionCount = 0;
    bool allTrackDefsHaveFixedComponent = true;
    for (int i = 0; i < templ.len; i++) {
        const GridTemplateComponent& c = templ[i];
        if (!c.isRepeat) {
            nonAutoRepeatingTrackCount =
                nonAutoRepeatingTrackCount < kMaxGridTracks
                    ? nonAutoRepeatingTrackCount + 1
                    : kMaxGridTracks;
            if (!c.single.HasFixedComponent()) {
                allTrackDefsHaveFixedComponent = false;
            }
            continue;
        }
        if (!c.repeat.count.IsAuto()) {
            uint64_t additional = (uint64_t)c.repeat.count.count *
                                  (uint64_t)c.repeat.TrackCount();
            nonAutoRepeatingTrackCount =
                (uint64_t)nonAutoRepeatingTrackCount + additional >
                        kMaxGridTracks
                    ? kMaxGridTracks
                    : nonAutoRepeatingTrackCount + (uint32_t)additional;
        } else {
            autoRepetitionCount += 1;
        }
        for (int k = 0; k < c.repeat.tracks.len; k++) {
            if (!c.repeat.tracks[k].HasFixedComponent()) {
                allTrackDefsHaveFixedComponent = false;
            }
        }
    }

    // More than one auto-repetition, or one mixed with non-fixed track sizing
    // functions, makes the template invalid: fall back to no explicit tracks.
    bool templateIsValid =
        autoRepetitionCount == 0 ||
        (autoRepetitionCount == 1 && allTrackDefsHaveFixedComponent);
    if (!templateIsValid) {
        return {};
    }

    if (autoRepetitionCount == 0) {
        return {0, (uint16_t)nonAutoRepeatingTrackCount};
    }

    const GridTemplateRepetition* repetition = nullptr;
    uint32_t autoRepeatInsertionPoint = 0;
    for (int i = 0; i < templ.len; i++) {
        if (templ[i].isRepeat && templ[i].repeat.count.IsAuto()) {
            repetition = &templ[i].repeat;
            break;
        }
        uint64_t additional = !templ[i].isRepeat
                                  ? 1u
                                  : (uint64_t)templ[i].repeat.count.count *
                                        templ[i].repeat.TrackCount();
        autoRepeatInsertionPoint =
            (uint64_t)autoRepeatInsertionPoint + additional > UINT32_MAX
                ? UINT32_MAX
                : autoRepeatInsertionPoint + (uint32_t)additional;
    }
    uint16_t repetitionTrackCount = repetition->TrackCount();
    if (repetitionTrackCount == 0) {
        return {};
    }

    uint32_t numRepetitions = 1;
    if (IsSome(autoFitContainerSize)) {
        float innerContainerSize = autoFitContainerSize;
        Optf parentSize = Some(innerContainerSize);

        float nonRepeatingTrackUsedSpace = 0.0f;
        for (int i = 0; i < templ.len; i++) {
            const GridTemplateComponent& c = templ[i];
            if (!c.isRepeat) {
                nonRepeatingTrackUsedSpace +=
                    TrackDefiniteValue(c.single, parentSize, calc);
                continue;
            }
            if (c.repeat.count.IsAuto()) {
                continue;
            }
            float sum = 0.0f;
            for (int k = 0; k < c.repeat.tracks.len; k++) {
                sum += TrackDefiniteValue(c.repeat.tracks[k], parentSize, calc);
            }
            nonRepeatingTrackUsedSpace += sum * (float)c.repeat.count.count;
        }

        SizeLp gapStyle = style.gap;
        LengthPercentage gapLp = gapStyle.GetAbs(axis);
        SizeLp asSize = {gapLp, gapLp};
        float gapSize = asSize.ResolveOrZero(Some(innerContainerSize), calc).w;

        float perRepetitionTrackUsedSpace = 0.0f;
        for (int k = 0; k < repetition->tracks.len; k++) {
            perRepetitionTrackUsedSpace +=
                TrackDefiniteValue(repetition->tracks[k], parentSize, calc);
        }

        // The first repetition is special-cased: how many gaps it contributes
        // depends on how many non-repeating tracks the template has.
        int gapCount =
            (int)nonAutoRepeatingTrackCount + (int)repetitionTrackCount - 1;
        if (gapCount < 0) {
            gapCount = 0;
        }
        float firstRepetitionAndNonRepeatingTracksUsedSpace =
            nonRepeatingTrackUsedSpace + perRepetitionTrackUsedSpace +
            (float)gapCount * gapSize;

        if (firstRepetitionAndNonRepeatingTracksUsedSpace >
            innerContainerSize) {
            // A single repetition already overflows; the count floors at one.
            numRepetitions = 1;
        } else {
            float perRepetitionGapUsedSpace =
                (float)repetitionTrackCount * gapSize;
            float perRepetitionUsedSpace =
                perRepetitionTrackUsedSpace + perRepetitionGapUsedSpace;
            if (perRepetitionUsedSpace <= 0.0f) {
                numRepetitions = UINT32_MAX;
            } else {
                float numRepetitionThatFit =
                    (innerContainerSize -
                     firstRepetitionAndNonRepeatingTracksUsedSpace) /
                    perRepetitionUsedSpace;
                // Plus the repetition already accounted for above.
                float rounded =
                    autoFitStrategy ==
                            AutoRepeatStrategy::MaxRepetitionsThatDoNotOverflow
                        ? floorf(numRepetitionThatFit)
                        : ceilf(numRepetitionThatFit);
                // 4294967040 is the greatest float below UINT32_MAX. Clamp
                // there so the conversion and the following +1 stay defined.
                numRepetitions = !isfinite(rounded) || rounded >= 4294967040.0f
                                     ? UINT32_MAX
                                 : rounded < 0.0f ? 1u
                                                  : (uint32_t)rounded + 1u;
            }
        }
    }

    uint32_t remainingTracks = autoRepeatInsertionPoint >= kMaxGridTracks
                                   ? 0
                                   : kMaxGridTracks - autoRepeatInsertionPoint;
    if (remainingTracks == 0) {
        numRepetitions = 0;
    } else {
        uint32_t maxRepetitions =
            (remainingTracks + repetitionTrackCount - 1) / repetitionTrackCount;
        numRepetitions = numRepetitions < 1 ? 1 : numRepetitions;
        numRepetitions =
            numRepetitions > maxRepetitions ? maxRepetitions : numRepetitions;
    }
    uint32_t gridTemplateTrackCount =
        nonAutoRepeatingTrackCount +
        (uint32_t)repetitionTrackCount * numRepetitions;
    gridTemplateTrackCount = gridTemplateTrackCount > kMaxGridTracks
                                 ? kMaxGridTracks
                                 : gridTemplateTrackCount;
    return {(uint16_t)numRepetitions, (uint16_t)gridTemplateTrackCount};
}

template <typename NextTrack>
void CreateImplicitTracks(Vec<GridTrack>* tracks, uint16_t count,
                          NextTrack nextTrack, LengthPercentage gap) {
    for (uint16_t i = 0; i < count; i++) {
        TrackSizingFunction def = nextTrack();
        VecAppend(*tracks, GridTrack::New(def.MinSizingFunction(),
                                          def.MaxSizingFunction()));
        VecAppend(*tracks, GridTrack::Gutter(gap));
    }
}

template <typename TrackHasItems>
void InitializeGridTracks(Vec<GridTrack>* tracks, TrackCounts counts,
                          const Style& style, AbsoluteAxis axis,
                          TrackHasItems trackHasItems) {
    Slice<GridTemplateComponent> trackTemplate;
    Slice<TrackSizingFunction> autoTracks;
    LengthPercentage gap;
    if (axis == AbsoluteAxis::Horizontal) {
        trackTemplate = style.gridTemplateColumns;
        autoTracks = style.gridAutoColumns;
        gap = style.gap.width;
    } else {
        trackTemplate = style.gridTemplateRows;
        autoTracks = style.gridAutoRows;
        gap = style.gap.height;
    }

    tracks->len = 0;
    VecAppend(*tracks, GridTrack::Gutter(gap));

    int autoTrackCount = autoTracks.len;
    uint64_t nonAutoRepeatingTrackCount = 0;
    for (int i = 0; i < trackTemplate.len; i++) {
        const GridTemplateComponent& c = trackTemplate[i];
        if (!c.isRepeat) {
            nonAutoRepeatingTrackCount += 1;
        } else if (!c.repeat.count.IsAuto()) {
            nonAutoRepeatingTrackCount +=
                (uint64_t)c.repeat.count.count * c.repeat.TrackCount();
        }
    }

    // Negative implicit tracks. Rust cycles the grid-auto-* list backwards
    // from the explicit grid, which is what the offset below does.
    if (counts.negativeImplicit > 0) {
        if (autoTrackCount == 0) {
            CreateImplicitTracks(
                tracks, counts.negativeImplicit,
                []() { return TrackSizingFunction::Auto(); }, gap);
        } else {
            int offset = autoTrackCount -
                         ((int)counts.negativeImplicit % autoTrackCount);
            int cursor = offset;
            CreateImplicitTracks(
                tracks, counts.negativeImplicit,
                [&]() {
                    TrackSizingFunction t = autoTracks[cursor % autoTrackCount];
                    cursor++;
                    return t;
                },
                gap);
        }
    }

    int currentTrackIndex = (int)counts.negativeImplicit;
    int explicitTrackLimit = (int)counts.negativeImplicit + (int)counts
                                                                .explicitCount;

    // Explicit tracks. The count is checked rather than the template being
    // empty, because a count of zero can come from an invalid template.
    if (counts.explicitCount > 0) {
        for (int i = 0; i < trackTemplate.len; i++) {
            const GridTemplateComponent& c = trackTemplate[i];
            if (!c.isRepeat) {
                if (currentTrackIndex >= explicitTrackLimit) {
                    continue;
                }
                VecAppend(*tracks,
                          GridTrack::New(c.single.MinSizingFunction(),
                                         c.single.MaxSizingFunction()));
                VecAppend(*tracks, GridTrack::Gutter(gap));
                currentTrackIndex += 1;
                continue;
            }
            if (!c.repeat.count.IsAuto()) {
                int total = (int)c.repeat.TrackCount() * (int)c.repeat.count
                                                             .count;
                for (int k = 0;
                     k < total && currentTrackIndex < explicitTrackLimit; k++) {
                    TrackSizingFunction f =
                        c.repeat.tracks[k % c.repeat.tracks.len];
                    VecAppend(*tracks, GridTrack::New(f.MinSizingFunction(),
                                                      f.MaxSizingFunction()));
                    VecAppend(*tracks, GridTrack::Gutter(gap));
                    currentTrackIndex += 1;
                }
                continue;
            }
            uint64_t nonAutoCount = nonAutoRepeatingTrackCount > INT_MAX
                                        ? INT_MAX
                                        : nonAutoRepeatingTrackCount;
            int autoRepeatedTrackCount =
                (int)counts.explicitCount - (int)nonAutoCount;
            if (autoRepeatedTrackCount < 0) {
                autoRepeatedTrackCount = 0;
            }
            bool isAutoFit = c.repeat.count
                                 .kind == RepetitionCount::Kind::AutoFit;
            for (int k = 0; k < autoRepeatedTrackCount &&
                            currentTrackIndex < explicitTrackLimit;
                 k++) {
                TrackSizingFunction def = c.repeat
                                              .tracks[k % c.repeat.tracks.len];
                GridTrack track = GridTrack::New(def.MinSizingFunction(),
                                                 def.MaxSizingFunction());
                GridTrack gutter = GridTrack::Gutter(gap);
                // An auto-fit track with no items in it collapses.
                if (isAutoFit && !trackHasItems(currentTrackIndex)) {
                    track.Collapse();
                    gutter.Collapse();
                }
                VecAppend(*tracks, track);
                VecAppend(*tracks, gutter);
                currentTrackIndex += 1;
            }

            // Collapsing an auto-fit track collapses the gutter after it but
            // not the one before, which is right so long as a non-collapsed
            // track follows somewhere. When the auto-repeat is the last thing
            // in the list there is none, so walk back to the first
            // non-collapsed track and collapse everything after it.
            bool isLast = currentTrackIndex == counts.Len();
            if (isAutoFit && isLast) {
                for (int t = tracks->len - 1; t >= 0; t--) {
                    GridTrack& prev = (*tracks)[t];
                    if (prev.kind == GridTrackKind::Track &&
                        !prev.isCollapsed) {
                        break;
                    }
                    prev.Collapse();
                }
            }
        }
    }

    int gridAreaTracks = (int)counts.negativeImplicit +
                         (int)counts.explicitCount - currentTrackIndex;
    if (gridAreaTracks < 0) {
        gridAreaTracks = 0;
    }
    uint16_t positive =
        (uint16_t)((int)counts.positiveImplicit + gridAreaTracks);
    if (autoTrackCount == 0) {
        CreateImplicitTracks(
            tracks, positive, []() { return TrackSizingFunction::Auto(); },
            gap);
    } else {
        int cursor = 0;
        CreateImplicitTracks(
            tracks, positive,
            [&]() {
                TrackSizingFunction t = autoTracks[cursor % autoTrackCount];
                cursor++;
                return t;
            },
            gap);
    }

    // The first and last grid lines are collapsed.
    if (tracks->len > 0) {
        (*tracks)[0].Collapse();
        (*tracks)[tracks->len - 1].Collapse();
    }
}

// ─── types/grid_item.rs, the parts that need the tree ────────────────────

// Which estimate of an other-axis track's size the child sizing functions are
// given. Rust passes a closure; there are exactly two of them.
enum class TrackSizeEstimate : uint8_t {
    // `track.max_track_sizing_function.definite_value(parent_size)`
    MaxTrackSizingFunction,
    // `Some(track.base_size)`
    BaseSize
};

Optf EstimateTrackSize(const GridTrack& track, Optf basis,
                       TrackSizeEstimate kind, CalcResolver calc) {
    if (kind == TrackSizeEstimate::BaseSize) {
        return Some(track.baseSize);
    }
    return track.maxTrackSizingFunction.DefiniteValue(basis, calc);
}

// The item's resolved margins for size contributions. A horizontal percentage
// margin always resolves to zero when the container size is indefinite, since
// otherwise it would be a cyclic dependency.
SizeF MarginsAxisSumsWithBaselineShims(const GridItem& item,
                                       Optf innerNodeWidth, CalcResolver calc) {
    RectLp zeroBasis;
    (void)zeroBasis;
    RectLpa m = item.margin;
    RectF r;
    // Rust resolves left/right against Some(0.0) and top/bottom against the
    // inner node width. That is not a typo: horizontal percentage margins are
    // deliberately zeroed.
    RectLpa horizontalOnly = {m.left, m.right, LengthPercentageAuto::Zero(),
                              LengthPercentageAuto::Zero()};
    RectF h = horizontalOnly.ResolveOrZero(Some(0.0f), calc);
    RectLpa verticalOnly = {LengthPercentageAuto::Zero(),
                            LengthPercentageAuto::Zero(), m.top, m.bottom};
    RectF v = verticalOnly.ResolveOrZero(innerNodeWidth, calc);
    r.left = h.left;
    r.right = h.right;
    r.top = v.top + item.baselineShim;
    r.bottom = v.bottom;
    return r.SumAxes();
}

// For an item spanning several tracks, the upper limit on its limited min-/
// max-content contribution: the sum of the fixed max sizing functions of the
// tracks it spans, applied only when it spans nothing but such tracks.
Optf SpannedTrackLimit(const GridItem& item, AbstractAxis axis,
                       const GridTrack* axisTracks, Optf axisParentSize,
                       CalcResolver calc) {
    int from = item.TrackRangeStart(axis);
    int to = item.TrackRangeEnd(axis);
    float limit = 0.0f;
    for (int i = from; i < to; i++) {
        Optf v = axisTracks[i]
                     .maxTrackSizingFunction
                     .DefiniteLimit(axisParentSize, calc);
        if (!IsSome(v)) {
            return None();
        }
        limit += v;
    }
    return Some(limit);
}

// The same, but excluding fit-content() arguments. Clamps automatic minimum
// contributions.
Optf SpannedFixedTrackLimit(const GridItem& item, AbstractAxis axis,
                            const GridTrack* axisTracks, Optf axisParentSize,
                            CalcResolver calc) {
    int from = item.TrackRangeStart(axis);
    int to = item.TrackRangeEnd(axis);
    float limit = 0.0f;
    for (int i = from; i < to; i++) {
        Optf v = axisTracks[i]
                     .maxTrackSizingFunction
                     .DefiniteValue(axisParentSize, calc);
        if (!IsSome(v)) {
            return None();
        }
        limit += v;
    }
    return Some(limit);
}

// The known_dimensions handed to the child sizing functions. The point of this
// is applying stretch alignment, which is what lets percentage sizes further
// down the tree resolve.
SizeFOpt ItemKnownDimensions(const GridItem& item, TaffyTree* tree,
                             SizeFOpt gridAreaSize) {
    CalcResolver calc = tree->calc;
    SizeF margins =
        MarginsAxisSumsWithBaselineShims(item, gridAreaSize.w, calc);
    Optf aspectRatio = item.aspectRatio;

    // CSS resolves percentage padding and border against the inline size of
    // the containing block, which for a grid item under intrinsic measurement
    // is the grid area's width when that is definite.
    RectF padding = item.padding.ResolveOrZero(gridAreaSize.w, calc);
    RectF border = item.border.ResolveOrZero(gridAreaSize.w, calc);
    SizeF paddingBorderSize = (padding + border).SumAxes();
    SizeF boxSizingAdjustment = item.boxSizing == BoxSizing::ContentBox
                                    ? paddingBorderSize
                                    : SizeF::Zero();
    SizeFOpt inherentSize =
        MaybeAdd(MaybeApplyAspectRatio(
                     item.size.MaybeResolve(gridAreaSize, calc), aspectRatio),
                 boxSizingAdjustment);
    // The size of the item is floored by its padding and border. An
    // unspecified minimum becomes that floor, and a smaller one is raised.
    SizeFOpt minSize = MaybeAdd(
        MaybeApplyAspectRatio(item.minSize.MaybeResolve(gridAreaSize, calc),
                              aspectRatio),
        boxSizingAdjustment);
    minSize = Or(minSize, paddingBorderSize);
    minSize = MaybeMax(minSize, paddingBorderSize);
    SizeFOpt maxSize = MaybeAdd(
        MaybeApplyAspectRatio(item.maxSize.MaybeResolve(gridAreaSize, calc),
                              aspectRatio),
        boxSizingAdjustment);

    SizeFOpt gridAreaMinusItemMargins =
        MaybeMax(MaybeSub(gridAreaSize, margins), SizeFOpt{0.0f, 0.0f});

    Optf width = inherentSize.w;
    if (!IsSome(width)) {
        // A sizing keyword is not auto, so it does not stretch. Stretch
        // itself is an exact size; the others resolve while measuring.
        if (item.size.width.IsSizingKeyword()) {
            SizingKeywordResolution keyword = ResolveSizingKeyword(
                item.size.width, gridAreaMinusItemMargins.w, gridAreaSize.w,
                calc);
            if (keyword.isSome && keyword.kind == SizingKeywordKind::Exact) {
                width = Some(keyword.exact);
            }
        } else if (!item.margin.left.IsAuto() && !item.margin.right.IsAuto() &&
                   item.justifySelf.IsStretchOrNormal()) {
            width = gridAreaMinusItemMargins.w;
        }
    }
    SizeFOpt sized =
        MaybeApplyAspectRatio(SizeFOpt{width, inherentSize.h}, aspectRatio);

    Optf height = sized.h;
    if (!IsSome(height)) {
        if (item.size.height.IsSizingKeyword()) {
            SizingKeywordResolution keyword = ResolveSizingKeyword(
                item.size.height, gridAreaMinusItemMargins.h, gridAreaSize.h,
                calc);
            if (keyword.isSome && keyword.kind == SizingKeywordKind::Exact) {
                height = Some(keyword.exact);
            }
        } else if (!item.margin.top.IsAuto() && !item.margin.bottom.IsAuto() &&
                   item.alignSelf.IsStretchOrNormal()) {
            height = gridAreaMinusItemMargins.h;
        }
    }
    sized = MaybeApplyAspectRatio(SizeFOpt{sized.w, height}, aspectRatio);
    return MaybeClamp(sized, minSize, maxSize);
}

// The grid area's size in the given axis when every spanned track is definite.
// Percentages on a grid item resolve against the grid area, not the container,
// so the area has to stay indefinite until its tracks are.
SizeFOpt ItemGridAreaSize(const GridItem& item, AbstractAxis axis,
                          const GridTrack* axisTracks,
                          const GridTrack* otherAxisTracks,
                          SizeFOpt availableSpace, TrackSizeEstimate estimate,
                          CalcResolver calc) {
    SizeFOpt size = SizeFOptNone();

    {
        float sum = 0.0f;
        bool definite = true;
        int from = item.TrackRangeStart(axis);
        int to = item.TrackRangeEnd(axis);
        for (int i = from; i < to; i++) {
            const GridTrack& t = axisTracks[i];
            Optf mn = t.minTrackSizingFunction
                          .DefiniteValue(Get(availableSpace, axis), calc);
            Optf mx = t.maxTrackSizingFunction
                          .DefiniteValue(Get(availableSpace, axis), calc);
            if (!IsSome(mn) || !IsSome(mx) || mn != mx) {
                definite = false;
                break;
            }
            sum += t.baseSize;
        }
        Set(&size, axis, definite ? Some(sum) : None());
    }

    {
        AbstractAxis other = Other(axis);
        float sum = 0.0f;
        bool definite = true;
        int from = item.TrackRangeStart(other);
        int to = item.TrackRangeEnd(other);
        for (int i = from; i < to; i++) {
            const GridTrack& t = otherAxisTracks[i];
            Optf v = EstimateTrackSize(t, Get(availableSpace, other), estimate,
                                       calc);
            if (!IsSome(v)) {
                definite = false;
                break;
            }
            sum += v + t.contentAlignmentAdjustment;
        }
        Set(&size, other, definite ? Some(sum) : None());
    }

    return size;
}

SizeFOpt ItemGridAreaSizeCached(GridItem* item, AbstractAxis axis,
                                const GridTrack* axisTracks,
                                const GridTrack* otherAxisTracks,
                                SizeFOpt availableSpace,
                                TrackSizeEstimate estimate, CalcResolver calc) {
    if (item->hasGridAreaSizeCache) {
        return item->gridAreaSizeCache;
    }
    SizeFOpt s = ItemGridAreaSize(*item, axis, axisTracks, otherAxisTracks,
                                  availableSpace, estimate, calc);
    item->gridAreaSizeCache = s;
    item->hasGridAreaSizeCache = true;
    return s;
}

// A min-content, max-content or fit-content size measures under that
// constraint. Stretch was already applied as a known size.
SizeAvail KeywordAdjustedAvailableSpace(const GridItem& item,
                                        SizeFOpt gridAreaSize,
                                        SizeAvail available, TaffyTree* tree) {
    if (!item.size.width.IsSizingKeyword() && !item.size.height
                                                   .IsSizingKeyword()) {
        return available;
    }
    SizeF margins =
        MarginsAxisSumsWithBaselineShims(item, gridAreaSize.w, tree->calc);
    SizeAvail adjusted = available;
    AbstractAxis axes[2] = {AbstractAxis::Inline, AbstractAxis::Block};
    for (int i = 0; i < 2; i++) {
        AbstractAxis axis = axes[i];
        Dimension sizeStyle = item.size.Get(axis);
        if (!sizeStyle.IsSizingKeyword()) {
            continue;
        }
        Optf stretch = MaybeMax(
            MaybeSub(Get(gridAreaSize, axis), Some(Get(margins, axis))), 0.0f);
        SizingKeywordResolution keyword = ResolveSizingKeyword(
            sizeStyle, stretch, Get(gridAreaSize, axis), tree->calc);
        if (keyword.isSome && keyword.kind == SizingKeywordKind::Measure) {
            adjusted.Set(axis, keyword.measure);
        }
    }
    return adjusted;
}

// Subtract the item's margins (and baseline shim) from the definite axes
// of the space available to the item's margin box. That is the space passed
// to the item. Margins resolve against the grid area's width, and an auto
// margin counts as zero, matching final positioning.
SizeFOpt AvailableSpaceMinusMargins(const GridItem& item, SizeFOpt gridAreaSize,
                                    SizeFOpt availableSpace, TaffyTree* tree) {
    if (!IsSome(availableSpace.w) && !IsSome(availableSpace.h)) {
        return availableSpace;
    }
    RectF margin = item.margin.ResolveOrZero(gridAreaSize.w, tree->calc);
    SizeFOpt out = availableSpace;
    if (IsSome(out.w)) {
        out.w = F32Max(out.w - margin.HorizontalAxisSum(), 0.0f);
    }
    if (IsSome(out.h)) {
        out.h =
            F32Max(out.h - margin.VerticalAxisSum() - item.baselineShim, 0.0f);
    }
    return out;
}

float ItemMinContentContribution(const GridItem& item, AbstractAxis axis,
                                 TaffyTree* tree, SizeFOpt gridAreaSize,
                                 SizeFOpt availableSpace) {
    SizeFOpt knownDimensions = ItemKnownDimensions(item, tree, gridAreaSize);
    // A size already known on this axis is the contribution. Measuring the
    // child would only repeat it.
    Optf known = Get(knownDimensions, axis);
    if (IsSome(known)) {
        return known;
    }
    // The child sees the grid area as its containing block during intrinsic
    // measurement, so percentage box properties resolve against it.
    // Margins come out before a sizing keyword rewrites the constraint.
    availableSpace =
        AvailableSpaceMinusMargins(item, gridAreaSize, availableSpace, tree);
    SizeAvail avail = {
        IsSome(availableSpace.w) ? AvailableSpace::Definite(availableSpace.w)
                                 : AvailableSpace::MinContent(),
        IsSome(availableSpace.h) ? AvailableSpace::Definite(availableSpace.h)
                                 : AvailableSpace::MinContent()};
    avail = KeywordAdjustedAvailableSpace(item, gridAreaSize, avail, tree);
    return tree->MeasureChildSize(item.node, knownDimensions, gridAreaSize,
                                  avail, SizingMode::InherentSize,
                                  AsAbsNaive(axis), LineBool::False());
}

float ItemMinContentContributionCached(GridItem* item, AbstractAxis axis,
                                       TaffyTree* tree, SizeFOpt gridAreaSize,
                                       SizeFOpt availableSpace) {
    Optf cached = Get(item->minContentContributionCache, axis);
    if (IsSome(cached)) {
        return cached;
    }
    float size = ItemMinContentContribution(*item, axis, tree, gridAreaSize,
                                            availableSpace);
    Set(&item->minContentContributionCache, axis, Some(size));
    return size;
}

float ItemMaxContentContribution(const GridItem& item, AbstractAxis axis,
                                 TaffyTree* tree, SizeFOpt gridAreaSize,
                                 SizeFOpt availableSpace) {
    SizeFOpt knownDimensions = ItemKnownDimensions(item, tree, gridAreaSize);
    Optf known = Get(knownDimensions, axis);
    if (IsSome(known)) {
        return known;
    }
    availableSpace =
        AvailableSpaceMinusMargins(item, gridAreaSize, availableSpace, tree);
    SizeAvail avail = {
        IsSome(availableSpace.w) ? AvailableSpace::Definite(availableSpace.w)
                                 : AvailableSpace::MaxContent(),
        IsSome(availableSpace.h) ? AvailableSpace::Definite(availableSpace.h)
                                 : AvailableSpace::MaxContent()};
    avail = KeywordAdjustedAvailableSpace(item, gridAreaSize, avail, tree);
    return tree->MeasureChildSize(item.node, knownDimensions, gridAreaSize,
                                  avail, SizingMode::InherentSize,
                                  AsAbsNaive(axis), LineBool::False());
}

float ItemMaxContentContributionCached(GridItem* item, AbstractAxis axis,
                                       TaffyTree* tree, SizeFOpt gridAreaSize,
                                       SizeFOpt availableSpace) {
    Optf cached = Get(item->maxContentContributionCache, axis);
    if (IsSome(cached)) {
        return cached;
    }
    float size = ItemMaxContentContribution(*item, axis, tree, gridAreaSize,
                                            availableSpace);
    Set(&item->maxContentContributionCache, axis, Some(size));
    return size;
}

// The smallest outer size an item can have. If its preferred size behaves as
// auto or depends on its containing block, this is the outer size that would
// result from using its minimum size as its preferred size; otherwise it is
// the min-content contribution.
// https://www.w3.org/TR/css-grid-1/#min-size-auto
float ItemMinimumContribution(GridItem* item, TaffyTree* tree,
                              AbstractAxis axis, const GridTrack* axisTracks,
                              int axisTrackCount, SizeFOpt gridAreaSize,
                              SizeFOpt innerNodeSize) {
    CalcResolver calc = tree->calc;
    RectF padding = item->padding.ResolveOrZero(gridAreaSize.w, calc);
    RectF border = item->border.ResolveOrZero(gridAreaSize.w, calc);
    SizeF paddingBorderSize = (padding + border).SumAxes();
    SizeF boxSizingAdjustment = item->boxSizing == BoxSizing::ContentBox
                                    ? paddingBorderSize
                                    : SizeF::Zero();

    Optf size = Get(MaybeAdd(MaybeApplyAspectRatio(
                                 item->size.MaybeResolve(gridAreaSize, calc),
                                 item->aspectRatio),
                             boxSizingAdjustment),
                    axis);
    if (!IsSome(size)) {
        // A percentage minimum is cyclic when the area it is a percentage of
        // is the track this contribution is sizing. An indefinite area
        // resolves that percentage against zero.
        SizeFOpt cyclicMinBasis = {Some(UnwrapOr(gridAreaSize.w, 0.0f)),
                                   Some(UnwrapOr(gridAreaSize.h, 0.0f))};
        size = Get(MaybeAdd(MaybeApplyAspectRatio(item->minSize.MaybeResolve(
                                                      cyclicMinBasis, calc),
                                                  item->aspectRatio),
                            boxSizingAdjustment),
                   axis);
    }
    if (!IsSome(size)) {
        Overflow o =
            axis == AbstractAxis::Inline ? item->overflow.x : item->overflow.y;
        size = MaybeIntoAutomaticMinSize(o);
    }
    if (!IsSome(size)) {
        // The automatic minimum size is content-based only if the item spans
        // at least one track whose min sizing function behaves as auto
        // (auto itself, or a percentage while this axis is indefinite), and
        // either it spans one track or none of the tracks it spans is
        // flexible. A flexible track elsewhere in the axis does not count.
        // Otherwise the automatic minimum is zero.
        (void)axisTrackCount;
        bool spansAutoMinTrack = false;
        bool spansAFlexibleTrack = false;
        int from = item->TrackRangeStart(axis);
        int to = item->TrackRangeEnd(axis);
        for (int i = from; i < to; i++) {
            if (axisTracks[i].minTrackSizingFunction.BehavesAsAuto(
                    Get(innerNodeSize, axis))) {
                spansAutoMinTrack = true;
            }
            if (axisTracks[i].maxTrackSizingFunction.IsFr()) {
                spansAFlexibleTrack = true;
            }
        }
        bool onlySpanOneTrack =
            (item->TrackRangeEnd(axis) - item->TrackRangeStart(axis)) == 1;
        bool useContentBasedMinimum =
            spansAutoMinTrack && (onlySpanOneTrack || !spansAFlexibleTrack);

        if (useContentBasedMinimum) {
            float minimumContribution = ItemMinContentContributionCached(
                item, axis, tree, gridAreaSize, gridAreaSize);
            // A compressible replaced element with a definite preferred or
            // maximum size is capped by it, with indefinite percentages
            // resolved against zero.
            if (item->isCompressibleReplaced) {
                Optf pref = item->size.Get(axis).MaybeResolve(Some(0.0f), calc);
                Optf mx = item->maxSize.Get(axis)
                              .MaybeResolve(Some(0.0f), calc);
                minimumContribution =
                    MaybeMin(MaybeMin(minimumContribution, pref), mx);
            }
            // The content-based minimum is clamped by the fixed max track
            // sizing functions the item spans. That clamp does not apply to
            // an explicitly specified preferred or minimum size, and a
            // fit-content() argument does not clamp the way a fixed max does.
            Optf limit = SpannedFixedTrackLimit(*item, axis, axisTracks,
                                                Get(innerNodeSize, axis), calc);
            size = Some(MaybeMin(minimumContribution, limit));
        } else {
            size = Some(0.0f);
        }
    }

    return UnwrapOr(size, 0.0f);
}

float ItemMinimumContributionCached(GridItem* item, TaffyTree* tree,
                                    AbstractAxis axis,
                                    const GridTrack* axisTracks,
                                    int axisTrackCount, SizeFOpt gridAreaSize,
                                    SizeFOpt innerNodeSize) {
    Optf cached = Get(item->minimumContributionCache, axis);
    if (IsSome(cached)) {
        return cached;
    }
    float size =
        ItemMinimumContribution(item, tree, axis, axisTracks, axisTrackCount,
                                gridAreaSize, innerNodeSize);
    Set(&item->minimumContributionCache, axis, Some(size));
    return size;
}

// ─── placement.rs ────────────────────────────────────────────────────────

OriginZeroLine AdvancePosition(OriginZeroLine position) {
    int32_t value = (int32_t)position.v + 1;
    value = value < INT16_MIN   ? INT16_MIN
            : value > INT16_MAX ? INT16_MAX
                                : value;
    return OriginZeroLine{(int16_t)value};
}

LineOzl ResolveIndefiniteGridSpan(OriginZeroLine position, uint16_t span) {
    auto line = [](int32_t value) {
        value = value < INT16_MIN   ? INT16_MIN
                : value > INT16_MAX ? INT16_MAX
                                    : value;
        return OriginZeroLine{(int16_t)value};
    };
    return {position, line((int32_t)position.v + span)};
}

// Items placed outside the limited grid are clamped into it.
// https://www.w3.org/TR/css-grid-1/#overlarge-grids
LineOzl ClampSpanToLimitedGrid(LineOzl span) {
    int32_t start = span.start.v;
    start = start<kMinOzLine ? kMinOzLine : start>(int32_t) kMaxOzLine - 1
                ? (int32_t)kMaxOzLine - 1
                : start;
    int32_t end = span.end.v;
    end = end<start + 1 ? start + 1 : end>(int32_t) kMaxOzLine
              ? (int32_t)kMaxOzLine
              : end;
    return {OriginZeroLine{(int16_t)start}, OriginZeroLine{(int16_t)end}};
}

// One child, as the placement algorithm sees it.
struct PlacementChild {
    int index = 0;
    NodeId node;
    LinePlain horizontal;
    LinePlain vertical;

    LinePlain Get(AbsoluteAxis a) const {
        return a == AbsoluteAxis::Horizontal ? horizontal : vertical;
    }
};

void RecordGridPlacement(CellOccupancyMatrix* matrix, Vec<GridItem>* items,
                         TaffyTree* tree, NodeId node, int index,
                         AlignItems parentAlignItems,
                         AlignItems parentJustifyItems,
                         AbsoluteAxis primaryAxis, LineOzl primarySpan,
                         LineOzl secondarySpan,
                         CellOccupancyState placementType) {
    primarySpan = ClampSpanToLimitedGrid(primarySpan);
    secondarySpan = ClampSpanToLimitedGrid(secondarySpan);
    matrix->MarkAreaAs(primaryAxis, primarySpan, secondarySpan, placementType);

    LineOzl colSpan =
        primaryAxis == AbsoluteAxis::Horizontal ? primarySpan : secondarySpan;
    LineOzl rowSpan =
        primaryAxis == AbsoluteAxis::Horizontal ? secondarySpan : primarySpan;

    const Style& s = tree->GetStyle(node);
    GridItem item;
    item.node = node;
    item.sourceOrder = (uint16_t)index;
    item.row = rowSpan;
    item.column = colSpan;
    item.isCompressibleReplaced = s.IsCompressibleReplaced();
    item.overflow = s.overflow;
    item.boxSizing = s.boxSizing;
    item.size = s.size;
    item.minSize = s.minSize;
    item.maxSize = s.maxSize;
    item.aspectRatio = s.aspectRatio;
    item.padding = s.padding;
    item.border = s.border;
    item.margin = s.margin;
    item.alignSelf = s.alignSelf.UnwrapOr(parentAlignItems);
    item.justifySelf = s.justifySelf.UnwrapOr(parentJustifyItems);
    VecAppend(*items, item);
}

LineOzl PlaceDefiniteGridItemAxis(const PlacementChild& child,
                                  AbsoluteAxis axis) {
    return child.Get(axis).ResolveDefiniteGridLines();
}

struct SpanPair {
    LineOzl primary;
    LineOzl secondary;
};

SpanPair PlaceDefiniteSecondaryAxisItem(const CellOccupancyMatrix& matrix,
                                        const PlacementChild& child,
                                        GridAutoFlow autoFlow) {
    AbsoluteAxis primaryAxis = PrimaryAxis(autoFlow);
    AbsoluteAxis secondaryAxis = OtherAxis(primaryAxis);
    OriginZeroLine primaryStart = matrix.Counts(primaryAxis)
                                      .ImplicitStartLine();

    LineOzl secondaryPlacement = child.Get(secondaryAxis)
                                     .ResolveDefiniteGridLines();

    OriginZeroLine startingPosition = primaryStart;
    if (!IsDense(autoFlow)) {
        OptOriginZeroLine lookup =
            matrix.AutoPlacementCursor(primaryAxis, secondaryPlacement.start);
        if (lookup.IsSome()) {
            startingPosition = lookup.val;
        }
    }
    uint16_t primarySpanLen = child.Get(primaryAxis).IndefiniteSpan();

    OriginZeroLine position = startingPosition;
    while (true) {
        LineOzl primaryPlacement =
            ResolveIndefiniteGridSpan(position, primarySpanLen);
        OptOriginZeroLine collision = matrix.LineAreaCollisionJump(
            primaryAxis, primaryPlacement, secondaryPlacement);
        if (!collision.IsSome()) {
            return {primaryPlacement, secondaryPlacement};
        }
        position = collision.val;
    }
}

SpanPair PlaceIndefinitelyPositionedItem(const CellOccupancyMatrix& matrix,
                                         const PlacementChild& child,
                                         GridAutoFlow autoFlow,
                                         OriginZeroLine gridPrimary,
                                         OriginZeroLine gridSecondary) {
    AbsoluteAxis primaryAxis = PrimaryAxis(autoFlow);
    AbsoluteAxis secondaryAxis = OtherAxis(primaryAxis);

    LinePlain primaryStyle = child.Get(primaryAxis);
    LinePlain secondaryStyle = child.Get(secondaryAxis);

    uint16_t secondarySpanLen = secondaryStyle.IndefiniteSpan();
    bool hasDefinitePrimary = primaryStyle.IsDefinite();
    OriginZeroLine primaryGridStart = matrix.Counts(primaryAxis)
                                          .ImplicitStartLine();
    OriginZeroLine primaryGridEnd = matrix.Counts(primaryAxis)
                                        .ImplicitEndLine();
    OriginZeroLine secondaryGridStart = matrix.Counts(secondaryAxis)
                                            .ImplicitStartLine();

    OriginZeroLine primaryIdx = gridPrimary;
    OriginZeroLine secondaryIdx = gridSecondary;

    if (hasDefinitePrimary) {
        LineOzl primarySpan = primaryStyle.ResolveDefiniteGridLines();
        if (IsDense(autoFlow)) {
            secondaryIdx = secondaryGridStart;
        } else if (primarySpan.start < primaryIdx) {
            secondaryIdx = AdvancePosition(secondaryIdx);
        }
        // A fixed primary position: step the secondary axis until it fits.
        while (true) {
            LineOzl secondarySpan =
                ResolveIndefiniteGridSpan(secondaryIdx, secondarySpanLen);
            OptOriginZeroLine collision = matrix.LineAreaCollisionJump(
                secondaryAxis, secondarySpan, primarySpan);
            if (!collision.IsSome()) {
                return {primarySpan, secondarySpan};
            }
            secondaryIdx = collision.val;
        }
    }

    uint16_t primarySpanLen = primaryStyle.IndefiniteSpan();
    // Neither axis is fixed: walk the primary axis until the end of the
    // existing tracks, then reset it and step the secondary axis.
    while (true) {
        LineOzl primarySpan =
            ResolveIndefiniteGridSpan(primaryIdx, primarySpanLen);
        LineOzl secondarySpan =
            ResolveIndefiniteGridSpan(secondaryIdx, secondarySpanLen);

        if (primarySpan.end > primaryGridEnd) {
            // A span that does not fit even at the search start can never
            // fit: searching only moves it further from the start. Return
            // and let the placement record clamp it into the limited grid.
            if (primaryIdx == primaryGridStart) {
                return {primarySpan, secondarySpan};
            }
            secondaryIdx = AdvancePosition(secondaryIdx);
            primaryIdx = primaryGridStart;
            continue;
        }
        OptOriginZeroLine collision = matrix.LineAreaCollisionJump(
            primaryAxis, primarySpan, secondarySpan);
        if (collision.IsSome()) {
            primaryIdx = collision.val;
            continue;
        }
        return {primarySpan, secondarySpan};
    }
}

// 8.5 Grid Item Placement Algorithm
// https://www.w3.org/TR/css-grid-2/#auto-placement-algo
void PlaceGridItems(CellOccupancyMatrix* matrix, Vec<GridItem>* items,
                    TaffyTree* tree, const Vec<PlacementChild>& children,
                    GridAutoFlow gridAutoFlow, AlignItems alignItems,
                    AlignItems justifyItems) {
    AbsoluteAxis primaryAxis = PrimaryAxis(gridAutoFlow);
    AbsoluteAxis secondaryAxis = OtherAxis(primaryAxis);

    // 1. Children with definite positions in both axes.
    for (int i = 0; i < len(children); i++) {
        const PlacementChild& c = children[i];
        if (!c.horizontal.IsDefinite() || !c.vertical.IsDefinite()) {
            continue;
        }
        LineOzl primarySpan = PlaceDefiniteGridItemAxis(c, primaryAxis);
        LineOzl secondarySpan = PlaceDefiniteGridItemAxis(c, secondaryAxis);
        RecordGridPlacement(matrix, items, tree, c.node, c.index, alignItems,
                            justifyItems, primaryAxis, primarySpan,
                            secondarySpan,
                            CellOccupancyState::DefinitelyPlaced);
    }

    // 2. Children with a definite secondary axis position only.
    for (int i = 0; i < len(children); i++) {
        const PlacementChild& c = children[i];
        if (!c.Get(secondaryAxis).IsDefinite() || c.Get(primaryAxis)
                                                      .IsDefinite()) {
            continue;
        }
        SpanPair spans =
            PlaceDefiniteSecondaryAxisItem(*matrix, c, gridAutoFlow);
        RecordGridPlacement(matrix, items, tree, c.node, c.index, alignItems,
                            justifyItems, primaryAxis, spans.primary,
                            spans.secondary, CellOccupancyState::AutoPlaced);
    }

    // 3. Determining the number of columns in the implicit grid is already
    //    covered: the size estimate pre-sizes the matrix, and MarkAreaAs
    //    expands it as needed.

    // 4. The remaining items. Placement stays in logical order; RTL is a
    //    matter of which physical edge a logical line maps to.
    OriginZeroLine startPrimary = matrix->Counts(primaryAxis)
                                      .ImplicitStartLine();
    OriginZeroLine startSecondary = matrix->Counts(secondaryAxis)
                                        .ImplicitStartLine();
    OriginZeroLine posPrimary = startPrimary;
    OriginZeroLine posSecondary = startSecondary;

    for (int i = 0; i < len(children); i++) {
        const PlacementChild& c = children[i];
        if (c.Get(secondaryAxis).IsDefinite()) {
            continue;
        }
        SpanPair spans = PlaceIndefinitelyPositionedItem(
            *matrix, c, gridAutoFlow, posPrimary, posSecondary);
        RecordGridPlacement(matrix, items, tree, c.node, c.index, alignItems,
                            justifyItems, primaryAxis, spans.primary,
                            spans.secondary, CellOccupancyState::AutoPlaced);

        // Dense packing restarts from the beginning for the next item; sparse
        // packing carries on from this one.
        if (IsDense(gridAutoFlow)) {
            posPrimary = startPrimary;
            posSecondary = startSecondary;
        } else {
            posPrimary = spans.primary.end;
            posSecondary = spans.secondary.start;
        }
    }
}

// ─── track_sizing.rs ─────────────────────────────────────────────────────

// Whether a minimum or a maximum size's space is being distributed. Controls
// what happens when distributing beyond limits.
// https://www.w3.org/TR/css-grid-1/#extra-space
enum class IntrinsicContributionType : uint8_t {
    Minimum,
    Maximum
};

// Walks a list of items sorted by (crosses a flex track, span) in batches of
// equal span, stopping once it reaches the flex-crossing items, which form one
// final batch.
struct ItemBatcher {
    AbstractAxis axis;
    int indexOffset = 0;
    uint16_t currentSpan = 1;
    bool currentIsFlex = false;

    bool Next(GridItem* items, int n, int* outStart, int* outEnd,
              bool* outIsFlex) {
        if (currentIsFlex || indexOffset >= n) {
            return false;
        }
        const GridItem& item = items[indexOffset];
        currentSpan = item.Span(axis);
        currentIsFlex = item.CrossesFlexibleTrack(axis);

        int nextIndexOffset = n;
        if (!currentIsFlex) {
            for (int i = 0; i < n; i++) {
                if (items[i].CrossesFlexibleTrack(axis) ||
                    items[i].Span(axis) > currentSpan) {
                    nextIndexOffset = i;
                    break;
                }
            }
        }
        *outStart = indexOffset;
        *outEnd = nextIndexOffset;
        *outIsFlex = currentIsFlex;
        indexOffset = nextIndexOffset;
        return true;
    }
};

// The variables the intrinsic size computations need, kept together so they do
// not have to be threaded through every call.
struct IntrinsicSizeMeasurer {
    TaffyTree* tree;
    const GridTrack* otherAxisTracks;
    TrackSizeEstimate estimate;
    AbstractAxis axis;
    SizeFOpt innerNodeSize = SizeFOptNone();

    CalcResolver Calc() const { return tree->calc; }

    SizeFOpt GridAreaSize(GridItem* item, const GridTrack* axisTracks) const {
        return ItemGridAreaSizeCached(item, axis, axisTracks, otherAxisTracks,
                                      innerNodeSize, estimate, tree->calc);
    }
    float MinContentContribution(GridItem* item,
                                 const GridTrack* axisTracks) const {
        SizeFOpt gridAreaSize = GridAreaSize(item, axisTracks);
        SizeFOpt availableSpace = gridAreaSize;
        Set(&availableSpace, axis, None());
        SizeF marginAxisSums = MarginsAxisSumsWithBaselineShims(
            *item, availableSpace.w, tree->calc);
        float contribution = ItemMinContentContributionCached(
            item, axis, tree, gridAreaSize, availableSpace);
        return contribution + Get(marginAxisSums, axis);
    }
    float MaxContentContribution(GridItem* item,
                                 const GridTrack* axisTracks) const {
        SizeFOpt gridAreaSize = GridAreaSize(item, axisTracks);
        SizeFOpt availableSpace = gridAreaSize;
        Set(&availableSpace, axis, None());
        SizeF marginAxisSums = MarginsAxisSumsWithBaselineShims(
            *item, availableSpace.w, tree->calc);
        float contribution = ItemMaxContentContributionCached(
            item, axis, tree, gridAreaSize, availableSpace);
        return contribution + Get(marginAxisSums, axis);
    }
    float MinimumContribution(GridItem* item, const GridTrack* axisTracks,
                              int axisTrackCount) const {
        SizeFOpt gridAreaSize = GridAreaSize(item, axisTracks);
        SizeFOpt availableSpace = gridAreaSize;
        Set(&availableSpace, axis, None());
        SizeF marginAxisSums = MarginsAxisSumsWithBaselineShims(
            *item, availableSpace.w, tree->calc);
        float contribution = ItemMinimumContributionCached(
            item, tree, axis, axisTracks, axisTrackCount, gridAreaSize,
            innerNodeSize);
        return contribution + Get(marginAxisSums, axis);
    }
};

// Track sizing wants the items in ascending order of span, with the items that
// cross a flexible track last.
bool CmpByCrossFlexThenSpanThenStart(const GridItem& a, const GridItem& b,
                                     AbstractAxis axis) {
    bool af = a.CrossesFlexibleTrack(axis);
    bool bf = b.CrossesFlexibleTrack(axis);
    if (af != bf) {
        return !af;
    }
    LineOzl pa = a.Placement(axis);
    LineOzl pb = b.Placement(axis);
    if (pa.Span() != pb.Span()) {
        return pa.Span() < pb.Span();
    }
    return pa.start < pb.start;
}

// When estimating the other-axis size of content-sized items, align-content
// and justify-content should be taken into account if the container and all
// the items in that axis have definite sizes. This is the per-gutter
// adjustment that comes out of that.
float ComputeAlignmentGutterAdjustment(AlignContent alignment,
                                       Optf axisInnerNodeSize,
                                       const GridTrack* tracks, int nTracks,
                                       TrackSizeEstimate estimate,
                                       CalcResolver calc) {
    if (nTracks <= 1) {
        return 0.0f;
    }
    // Items never cross the outermost gutters, so Start and End can be treated
    // alike. The safety modifier does not change a gutter's weight; the
    // overflow fallback happens when offsets are computed.
    int outerGutterWeight = 0;
    int innerGutterWeight = 0;
    switch (alignment.Keyword()) {
        case AlignContentKeyword::Start:
        case AlignContentKeyword::FlexStart:
        case AlignContentKeyword::End:
        case AlignContentKeyword::FlexEnd:
        case AlignContentKeyword::Center:
            outerGutterWeight = 1;
            innerGutterWeight = 0;
            break;
        case AlignContentKeyword::Normal:
        case AlignContentKeyword::Stretch:
            outerGutterWeight = 0;
            innerGutterWeight = 0;
            break;
        case AlignContentKeyword::SpaceBetween:
            outerGutterWeight = 0;
            innerGutterWeight = 1;
            break;
        case AlignContentKeyword::SpaceAround:
            outerGutterWeight = 1;
            innerGutterWeight = 2;
            break;
        case AlignContentKeyword::SpaceEvenly:
            outerGutterWeight = 1;
            innerGutterWeight = 1;
            break;
    }
    if (innerGutterWeight == 0) {
        return 0.0f;
    }
    if (!IsSome(axisInnerNodeSize)) {
        return 0.0f;
    }

    float trackSizeSum = 0.0f;
    bool definite = true;
    for (int i = 0; i < nTracks; i++) {
        Optf v =
            EstimateTrackSize(tracks[i], axisInnerNodeSize, estimate, calc);
        if (!IsSome(v)) {
            definite = false;
            break;
        }
        trackSizeSum += v;
    }
    float freeSpace =
        definite ? F32Max(0.0f, axisInnerNodeSize - trackSizeSum) : 0.0f;

    int weightedTrackCount =
        (((nTracks - 3) / 2) * innerGutterWeight) + (2 * outerGutterWeight);
    if (weightedTrackCount == 0) {
        return 0.0f;
    }
    return (freeSpace / (float)weightedTrackCount) * (float)innerGutterWeight;
}

void ResolveItemTrackIndexes(GridItem* items, int n, TrackCounts columnCounts,
                             TrackCounts rowCounts) {
    for (int i = 0; i < n; i++) {
        GridItem& item = items[i];
        item.columnIndexes.start =
            (uint16_t)IntoTrackVecIndex(item.column.start, columnCounts);
        item.columnIndexes
            .end = (uint16_t)IntoTrackVecIndex(item.column.end, columnCounts);
        item.rowIndexes
            .start = (uint16_t)IntoTrackVecIndex(item.row.start, rowCounts);
        item.rowIndexes
            .end = (uint16_t)IntoTrackVecIndex(item.row.end, rowCounts);
    }
}

void DetermineIfItemCrossesFlexibleOrIntrinsicTracks(GridItem* items, int n,
                                                     const GridTrack* columns,
                                                     const GridTrack* rows) {
    for (int i = 0; i < n; i++) {
        GridItem& item = items[i];
        item.crossesFlexibleColumn = false;
        item.crossesIntrinsicColumn = false;
        for (int k = item.TrackRangeStart(AbstractAxis::Inline);
             k < item.TrackRangeEnd(AbstractAxis::Inline); k++) {
            if (columns[k].IsFlexible()) {
                item.crossesFlexibleColumn = true;
            }
            if (columns[k].HasIntrinsicSizingFunction()) {
                item.crossesIntrinsicColumn = true;
            }
        }
        item.crossesFlexibleRow = false;
        item.crossesIntrinsicRow = false;
        for (int k = item.TrackRangeStart(AbstractAxis::Block);
             k < item.TrackRangeEnd(AbstractAxis::Block); k++) {
            if (rows[k].IsFlexible()) {
                item.crossesFlexibleRow = true;
            }
            if (rows[k].HasIntrinsicSizingFunction()) {
                item.crossesIntrinsicRow = true;
            }
        }
    }
}

void FlushPlannedBaseSizeIncreases(GridTrack* tracks, int n) {
    for (int i = 0; i < n; i++) {
        tracks[i].baseSize += tracks[i].baseSizePlannedIncrease;
        tracks[i].baseSizePlannedIncrease = 0.0f;
    }
}

void FlushPlannedGrowthLimitIncreases(GridTrack* tracks, int n,
                                      bool setInfinitelyGrowable) {
    for (int i = 0; i < n; i++) {
        GridTrack& t = tracks[i];
        if (t.growthLimitPlannedIncrease > 0.0f) {
            t.growthLimit = t.growthLimit == INFINITY
                                ? t.baseSize + t.growthLimitPlannedIncrease
                                : t.growthLimit + t.growthLimitPlannedIncrease;
            t.infinitelyGrowable = setInfinitelyGrowable;
        } else {
            t.infinitelyGrowable = false;
        }
        t.growthLimitPlannedIncrease = 0.0f;
    }
}

// 11.4 Initialise Track sizes
void InitializeTrackSizes(TaffyTree* tree, GridTrack* tracks, int n,
                          Optf axisInnerNodeSize) {
    CalcResolver calc = tree->calc;
    for (int i = 0; i < n; i++) {
        GridTrack& t = tracks[i];
        // A fixed min sizing function resolves to a length and becomes the
        // initial base size; an intrinsic one starts at zero.
        t.baseSize = UnwrapOr(t.minTrackSizingFunction
                                  .DefiniteValue(axisInnerNodeSize, calc),
                              0.0f);
        // A fixed max sizing function becomes the initial growth limit; an
        // intrinsic or flexible one starts at infinity.
        t.growthLimit = UnwrapOr(t.maxTrackSizingFunction
                                     .DefiniteValue(axisInnerNodeSize, calc),
                                 INFINITY);
        if (t.growthLimit < t.baseSize) {
            t.growthLimit = t.baseSize;
        }
    }
}

// 11.5.1 Shim baseline-aligned items so their intrinsic size contributions
// reflect their baseline alignment.
void ResolveItemBaselines(TaffyTree* tree, AbstractAxis axis, GridItem* items,
                          int n, SizeFOpt innerNodeSize) {
    AbstractAxis otherAxis = Other(axis);
    StableSort(items, n, [&](const GridItem& a, const GridItem& b) {
        return a.Placement(otherAxis).start < b.Placement(otherAxis).start;
    });

    int start = 0;
    while (start < n) {
        OriginZeroLine currentRow = items[start].Placement(otherAxis).start;
        int end = start;
        while (end < n && items[end].Placement(otherAxis).start == currentRow) {
            end++;
        }

        // One or zero participating items makes baseline alignment a no-op.
        int baselineCount = 0;
        for (int i = start; i < end; i++) {
            if (items[i].ParticipatesInBaselineAlignment()) {
                baselineCount++;
            }
        }
        if (baselineCount <= 1) {
            start = end;
            continue;
        }

        for (int i = start; i < end; i++) {
            GridItem& item = items[i];
            if (!item.ParticipatesInBaselineAlignment()) {
                continue;
            }
            LayoutOutput out = tree->PerformChildLayout(
                item.node, SizeFOptNone(), innerNodeSize,
                SizeAvail::MinContent(), SizingMode::InherentSize,
                LineBool::False());
            RectLpa topOnly = {LengthPercentageAuto::Zero(),
                               LengthPercentageAuto::Zero(), item.margin.top,
                               LengthPercentageAuto::Zero()};
            float marginTop = topOnly.ResolveOrZero(innerNodeSize.w, tree->calc)
                                  .top;
            // A scroll container's baseline is its content at the initial
            // scroll position, clamped to the border box.
            // https://github.com/w3c/csswg-drafts/issues/7660
            float baseline = UnwrapOr(out.baselines.first, out.size.h);
            if (IsScrollContainer(item.overflow.y)) {
                baseline = F32Max(0.0f, F32Min(baseline, out.size.h));
            }
            item.baseline = Some(baseline + marginTop);
        }

        float rowMaxBaseline = 0.0f;
        for (int i = start; i < end; i++) {
            if (!items[i].ParticipatesInBaselineAlignment()) {
                continue;
            }
            rowMaxBaseline =
                F32Max(rowMaxBaseline, UnwrapOr(items[i].baseline, 0.0f));
        }
        for (int i = start; i < end; i++) {
            if (!items[i].ParticipatesInBaselineAlignment()) {
                continue;
            }
            items[i].baselineShim =
                rowMaxBaseline - UnwrapOr(items[i].baseline, 0.0f);
        }
        start = end;
    }
}

// Distribute space to tracks evenly, up to their limits. Used by both the
// base-size and the maximise-tracks steps.
template <typename Affected, typename Proportion, typename Property,
          typename Limit>
float DistributeSpaceUpToLimits(float spaceToDistribute, GridTrack* tracks,
                                int n, Affected trackIsAffected,
                                Proportion trackDistributionProportion,
                                Property trackAffectedProperty,
                                Limit trackLimit) {
    // A small constant so rounding error cannot spin this loop forever.
    const float kThreshold = 0.01f;

    // Each iteration freezes at least one track at its limit, so the loop
    // completes within n + 1 iterations. Non-finite values can prevent any
    // space from being distributed, so the bound guarantees termination.
    int maxIterations = n + 1;
    for (int iter = 0; iter < maxIterations; iter++) {
        if (spaceToDistribute <= kThreshold) {
            break;
        }
        float proportionSum = 0.0f;
        for (int i = 0; i < n; i++) {
            const GridTrack& t = tracks[i];
            if (trackAffectedProperty(t) + t.itemIncurredIncrease <
                    trackLimit(t) &&
                trackIsAffected(t)) {
                proportionSum += trackDistributionProportion(t);
            }
        }
        if (proportionSum == 0.0f) {
            break;
        }

        bool haveMin = false;
        float minIncreaseLimit = 0.0f;
        for (int i = 0; i < n; i++) {
            const GridTrack& t = tracks[i];
            if (trackAffectedProperty(t) + t.itemIncurredIncrease <
                    trackLimit(t) &&
                trackIsAffected(t)) {
                float v = (trackLimit(t) - trackAffectedProperty(t) -
                           t.itemIncurredIncrease) /
                          trackDistributionProportion(t);
                if (!haveMin || v < minIncreaseLimit) {
                    minIncreaseLimit = v;
                    haveMin = true;
                }
            }
        }
        if (!haveMin) {
            break;
        }
        float iterationIncrease =
            F32Min(minIncreaseLimit, spaceToDistribute / proportionSum);

        for (int i = 0; i < n; i++) {
            GridTrack& t = tracks[i];
            if (!trackIsAffected(t)) {
                continue;
            }
            float increase = iterationIncrease * trackDistributionProportion(t);
            if (increase > 0.0f &&
                trackAffectedProperty(t) + t.itemIncurredIncrease + increase <=
                    trackLimit(t) + kThreshold) {
                t.itemIncurredIncrease += increase;
                spaceToDistribute -= increase;
            }
        }
    }
    return spaceToDistribute;
}

// 11.5.1 Distributing Extra Space Across Spanned Tracks
// https://www.w3.org/TR/css-grid-1/#extra-space
template <typename Affected, typename Proportion, typename Limit>
void DistributeItemSpaceToBaseSizeInner(
    float space, GridTrack* tracks, int n, Affected trackIsAffected,
    Proportion trackDistributionProportion, Limit trackLimit,
    IntrinsicContributionType intrinsicContributionType) {
    if (space == 0.0f) {
        return;
    }
    bool anyAffected = false;
    for (int i = 0; i < n; i++) {
        if (trackIsAffected(tracks[i])) {
            anyAffected = true;
            break;
        }
    }
    if (!anyAffected) {
        return;
    }

    auto getBaseSize = [](const GridTrack& t) { return t.baseSize; };

    // 1. Find the space to distribute.
    float trackSizes = 0.0f;
    for (int i = 0; i < n; i++) {
        trackSizes += tracks[i].baseSize;
    }
    float extraSpace = F32Max(0.0f, space - trackSizes);

    // 2. Distribute up to the limits.
    const float kThreshold = 0.000001f;
    extraSpace = DistributeSpaceUpToLimits(
        extraSpace, tracks, n, trackIsAffected, trackDistributionProportion,
        getBaseSize, trackLimit);

    // 3. Distribute what remains beyond the limits.
    if (extraSpace > kThreshold) {
        // Accommodating minimum or min-content contributions: any affected
        // track that also has an intrinsic max sizing function. Accommodating
        // max-content contributions: any that also has a max-content max
        // sizing function.
        auto minimumFilter = [](const GridTrack& t) {
            return t.maxTrackSizingFunction.IsIntrinsic();
        };
        auto maximumFilter = [](const GridTrack& t) {
            return t.maxTrackSizingFunction.IsMaxOrFitContent();
        };
        int count = 0;
        for (int i = 0; i < n; i++) {
            const GridTrack& t = tracks[i];
            bool matches =
                intrinsicContributionType == IntrinsicContributionType::Minimum
                    ? minimumFilter(t)
                    : maximumFilter(t);
            if (trackIsAffected(t) && matches) {
                count++;
            }
        }
        // With no such tracks, use all the affected ones.
        bool useAll = count == 0;
        auto filter = [&](const GridTrack& t) {
            if (!trackIsAffected(t)) {
                return false;
            }
            if (useAll) {
                return true;
            }
            return intrinsicContributionType ==
                           IntrinsicContributionType::Minimum
                       ? minimumFilter(t)
                       : maximumFilter(t);
        };
        DistributeSpaceUpToLimits(
            extraSpace, tracks, n, filter, trackDistributionProportion,
            getBaseSize,
            [](const GridTrack& t) { return t.FitContentLimit(); });
    }

    // 4. Roll each track's item-incurred increase into its planned increase.
    for (int i = 0; i < n; i++) {
        GridTrack& t = tracks[i];
        if (t.itemIncurredIncrease > t.baseSizePlannedIncrease) {
            t.baseSizePlannedIncrease = t.itemIncurredIncrease;
        }
        t.itemIncurredIncrease = 0.0f;
    }
}

template <typename Affected, typename Limit>
void DistributeItemSpaceToBaseSize(
    bool isFlex, bool, float space, GridTrack* tracks, int n,
    Affected trackIsAffected, Limit trackLimit,
    IntrinsicContributionType intrinsicContributionType) {
    auto one = [](const GridTrack&) { return 1.0f; };
    if (isFlex) {
        auto filter = [&](const GridTrack& t) {
            return t.IsFlexible() && trackIsAffected(t);
        };
        float flexFactorSum = 0.0f;
        for (int i = 0; i < n; i++) {
            if (filter(tracks[i])) {
                flexFactorSum += tracks[i].FlexFactor();
            }
        }
        if (flexFactorSum > 0.0f) {
            auto flexFactor = [](const GridTrack& t) { return t.FlexFactor(); };
            DistributeItemSpaceToBaseSizeInner(space, tracks, n, filter,
                                               flexFactor, trackLimit,
                                               intrinsicContributionType);
        } else {
            DistributeItemSpaceToBaseSizeInner(space, tracks, n, filter, one,
                                               trackLimit,
                                               intrinsicContributionType);
        }
        return;
    }
    DistributeItemSpaceToBaseSizeInner(space, tracks, n, trackIsAffected, one,
                                       trackLimit, intrinsicContributionType);
}

// The same, simplified, for growth limits.
template <typename Affected>
void DistributeItemSpaceToGrowthLimit(float space, GridTrack* tracks, int n,
                                      Affected trackIsAffected) {
    if (space == 0.0f) {
        return;
    }
    int affected = 0;
    for (int i = 0; i < n; i++) {
        if (trackIsAffected(tracks[i])) {
            affected++;
        }
    }
    if (affected == 0) {
        return;
    }

    // 1. Find the space to distribute.
    float trackSizes = 0.0f;
    for (int i = 0; i < n; i++) {
        trackSizes += tracks[i].growthLimit == INFINITY ? tracks[i].baseSize
                                                        : tracks[i].growthLimit;
    }
    float extraSpace = F32Max(0.0f, space - trackSizes);

    // 2. Distribute up to the limits. A growable track is one marked
    // infinitely growable, or one whose growth limit is still infinite. Its
    // limit is the fit-content() argument when it has one. A track that is
    // not growable has already reached its growth limit, so it takes nothing
    // in this step.
    auto growthLimitOrBaseSize = [](const GridTrack& t) {
        return t.growthLimit == INFINITY ? t.baseSize : t.growthLimit;
    };
    auto one = [](const GridTrack&) { return 1.0f; };
    auto fitContentLimit = [](const GridTrack& t) {
        return t.FitContentLimit();
    };
    auto growable = [&](const GridTrack& t) {
        return trackIsAffected(t) &&
               (t.infinitelyGrowable || t.growthLimit == INFINITY);
    };
    extraSpace =
        DistributeSpaceUpToLimits(extraSpace, tracks, n, growable, one,
                                  growthLimitOrBaseSize, fitContentLimit);

    // 3. Whatever remains is distributed beyond the limits, to every
    // affected track.
    if (extraSpace > 0.0f) {
        DistributeSpaceUpToLimits(extraSpace, tracks, n, trackIsAffected, one,
                                  growthLimitOrBaseSize, fitContentLimit);
    }

    // 4. Roll each track's item-incurred increase into its planned increase.
    for (int i = 0; i < n; i++) {
        GridTrack& t = tracks[i];
        if (t.itemIncurredIncrease > t.growthLimitPlannedIncrease) {
            t.growthLimitPlannedIncrease = t.itemIncurredIncrease;
        }
        t.itemIncurredIncrease = 0.0f;
    }
}

// 11.5 Resolve Intrinsic Track Sizes
// Sum of the flex factors of the flexible tracks an item spans. A 0fr
// track is flexible and contributes zero.
float CrossedFlexFactorSum(const GridTrack* tracks, int count) {
    float sum = 0.0f;
    for (int i = 0; i < count; i++) {
        if (tracks[i].maxTrackSizingFunction.IsFr()) {
            sum += tracks[i].FlexFactor();
        }
    }
    return sum;
}

float InflexibleTrackBaseSizes(const GridTrack* tracks, int count) {
    float sum = 0.0f;
    for (int i = 0; i < count; i++) {
        if (!tracks[i].maxTrackSizingFunction.IsFr()) {
            sum += tracks[i].baseSize;
        }
    }
    return sum;
}

void ResolveIntrinsicTrackSizes(TaffyTree* tree, AbstractAxis axis,
                                GridTrack* axisTracks, int nAxisTracks,
                                const GridTrack* otherAxisTracks,
                                GridItem* items, int nItems,
                                AvailableSpace axisAvailableGridSpace,
                                SizeFOpt innerNodeSize,
                                TrackSizeEstimate estimate) {
    // Step 1 (shimming baseline-aligned items) is already done.

    // The algorithm walks items in ascending order of the number of tracks
    // they span, so they are pre-sorted into that order here.
    StableSort(items, nItems, [&](const GridItem& a, const GridItem& b) {
        return CmpByCrossFlexThenSpanThenStart(a, b, axis);
    });

    Optf axisInnerNodeSize = Get(innerNodeSize, axis);
    float flexFactorSum = 0.0f;
    for (int i = 0; i < nAxisTracks; i++) {
        flexFactorSum += axisTracks[i].FlexFactor();
    }
    IntrinsicSizeMeasurer sizer = {tree, otherAxisTracks, estimate, axis,
                                   innerNodeSize};
    CalcResolver calc = tree->calc;

    ItemBatcher batcher;
    batcher.axis = axis;
    int batchStart = 0;
    int batchEnd = 0;
    bool isFlex = false;
    while (batcher.Next(items, nItems, &batchStart, &batchEnd, &isFlex)) {
        GridItem* batch = items + batchStart;
        int batchLen = batchEnd - batchStart;
        uint16_t batchSpan = batch[0].Placement(axis).Span();

        // 2. Size tracks to fit non-spanning items.
        if (!isFlex && batchSpan == 1) {
            for (int bi = 0; bi < batchLen; bi++) {
                GridItem* item = &batch[bi];
                int trackIndex = (int)item->PlacementIndexes(axis).start + 1;
                const GridTrack& track = axisTracks[trackIndex];
                Overflow axisOverflow = axis == AbstractAxis::Inline
                                            ? item->overflow.x
                                            : item->overflow.y;

                float newBaseSize = track.baseSize;
                uint64_t minTag = track.minTrackSizingFunction.raw.Tag();
                if (minTag == CompactLength::kMinContentTag) {
                    newBaseSize =
                        F32Max(track.baseSize,
                               sizer.MinContentContribution(item, axisTracks));
                } else if (minTag == CompactLength::kMaxContentTag) {
                    newBaseSize =
                        F32Max(track.baseSize,
                               sizer.MaxContentContribution(item, axisTracks));
                } else if (track.minTrackSizingFunction
                               .BehavesAsAuto(axisInnerNodeSize)) {
                    // An indefinite container makes a percentage (or calc())
                    // min track behave as auto, not as min-content.
                    float space;
                    bool minOrMaxConstraint =
                        axisAvailableGridSpace
                                .kind == AvailableSpace::Kind::MinContent ||
                        axisAvailableGridSpace
                                .kind == AvailableSpace::Kind::MaxContent;
                    // QUIRK: the spec says a container sized under a min-
                    // or max-content constraint uses the items' limited
                    // min-content contributions in place of their minimum
                    // contributions. Browsers only do that when the item
                    // is not a scroll container, giving a scroll
                    // container's automatic minimum size (zero)
                    // precedence.
                    if (minOrMaxConstraint &&
                        !IsScrollContainer(axisOverflow)) {
                        float axisMinimumSize = sizer.MinimumContribution(
                            item, axisTracks, nAxisTracks);
                        float axisMinContentSize =
                            sizer.MinContentContribution(item, axisTracks);
                        Optf limit = track.maxTrackSizingFunction.DefiniteLimit(
                            axisInnerNodeSize, calc);
                        space = F32Max(MaybeMin(axisMinContentSize, limit),
                                       axisMinimumSize);
                    } else {
                        space = sizer.MinimumContribution(item, axisTracks,
                                                          nAxisTracks);
                    }
                    newBaseSize = F32Max(track.baseSize, space);
                }

                GridTrack& t = axisTracks[trackIndex];
                t.baseSize = newBaseSize;

                // Contributions call into the child, so they are computed
                // only when a sizing function uses them. Min-content is still
                // computed for every item in an intrinsic track: the re-run
                // check compares against the value cached by that call.
                MaxTrackSizingFunction maxFn = t.maxTrackSizingFunction;
                bool hasMinContent = t.HasIntrinsicSizingFunction();
                float minContentContribution = 0.0f;
                if (hasMinContent) {
                    minContentContribution =
                        sizer.MinContentContribution(item, axisTracks);
                }
                bool havePlanned = false;
                float planned = 0.0f;
                if (maxFn.IsFitContent()) {
                    float fitContentLimit = t.FitContentLimit();
                    float maxContentContribution =
                        F32Min(sizer.MaxContentContribution(item, axisTracks),
                               fitContentLimit);
                    // A non-scroll item also raises the limit to its
                    // min-content contribution.
                    if (hasMinContent && !IsScrollContainer(axisOverflow)) {
                        planned = F32Max(minContentContribution,
                                         maxContentContribution);
                    } else {
                        planned = maxContentContribution;
                    }
                    havePlanned = true;
                } else if (maxFn.IsMaxContentAlike() ||
                           (maxFn.UsesPercentage() &&
                            !IsSome(axisInnerNodeSize))) {
                    // An indefinite, not-yet-resolved container size makes a
                    // percentage track behave as auto, matching Chrome.
                    planned = sizer.MaxContentContribution(item, axisTracks);
                    havePlanned = true;
                } else if (maxFn.IsIntrinsic()) {
                    if (hasMinContent) {
                        planned = minContentContribution;
                        havePlanned = true;
                    }
                }
                if (havePlanned) {
                    t.growthLimitPlannedIncrease =
                        F32Max(t.growthLimitPlannedIncrease, planned);
                }
            }

            for (int i = 0; i < nAxisTracks; i++) {
                GridTrack& t = axisTracks[i];
                if (t.growthLimitPlannedIncrease > 0.0f) {
                    t.growthLimit = t.growthLimit == INFINITY
                                        ? t.growthLimitPlannedIncrease
                                        : F32Max(t.growthLimit,
                                                 t.growthLimitPlannedIncrease);
                }
                t.infinitelyGrowable = false;
                t.growthLimitPlannedIncrease = 0.0f;
                if (t.growthLimit < t.baseSize) {
                    t.growthLimit = t.baseSize;
                }
            }
            continue;
        }

        bool useFlexFactorForDistribution = isFlex && flexFactorSum != 0.0f;

        // 1. For intrinsic minimums.
        for (int bi = 0; bi < batchLen; bi++) {
            GridItem* item = &batch[bi];
            if (!item->CrossesIntrinsicTrack(axis)) {
                continue;
            }
            Overflow axisOverflow = axis == AbstractAxis::Inline
                                        ? item->overflow.x
                                        : item->overflow.y;
            bool minOrMaxConstraint =
                axisAvailableGridSpace
                        .kind == AvailableSpace::Kind::MinContent ||
                axisAvailableGridSpace.kind == AvailableSpace::Kind::MaxContent;
            float space;
            if (minOrMaxConstraint && !IsScrollContainer(axisOverflow)) {
                float axisMinimumSize =
                    sizer.MinimumContribution(item, axisTracks, nAxisTracks);
                float axisMinContentSize =
                    sizer.MinContentContribution(item, axisTracks);
                Optf limit = SpannedTrackLimit(*item, axis, axisTracks,
                                               axisInnerNodeSize, calc);
                float limitedMinContent = F32Max(
                    MaybeMin(axisMinContentSize, limit), axisMinimumSize);
                // CSS Grid 2 §12.5: an item crossing flexible tracks hands
                // those tracks only its flex-factor share of the content
                // left after the inflexible tracks, clamped at one. The
                // minimum contribution floors that, so a definite size
                // still fills a 0fr track while min-height:0 lets it
                // collapse.
                if (isFlex) {
                    int from = item->TrackRangeStart(axis);
                    int count = item->TrackRangeEnd(axis) - from;
                    float inflexible =
                        InflexibleTrackBaseSizes(axisTracks + from, count);
                    float scale = F32Min(
                        CrossedFlexFactorSum(axisTracks + from, count), 1.0f);
                    float excess = F32Max(limitedMinContent - inflexible, 0.0f);
                    space =
                        F32Max(axisMinimumSize, inflexible + excess * scale);
                } else {
                    space = limitedMinContent;
                }
            } else {
                space = sizer
                            .MinimumContribution(item, axisTracks, nAxisTracks);
            }
            int from = item->TrackRangeStart(axis);
            int count = item->TrackRangeEnd(axis) - from;
            if (space > 0.0f && count > 0) {
                auto hasIntrinsicMin = [&](const GridTrack& t) {
                    return !IsSome(t.minTrackSizingFunction
                                       .DefiniteValue(axisInnerNodeSize, calc));
                };
                if (IsScrollContainer(axisOverflow)) {
                    DistributeItemSpaceToBaseSize(
                        isFlex, useFlexFactorForDistribution, space,
                        axisTracks + from, count, hasIntrinsicMin,
                        [](const GridTrack& t) {
                            return t.FitContentLimitedGrowthLimit();
                        },
                        IntrinsicContributionType::Minimum);
                } else {
                    DistributeItemSpaceToBaseSize(
                        isFlex, useFlexFactorForDistribution, space,
                        axisTracks + from, count, hasIntrinsicMin,
                        [](const GridTrack& t) { return t.growthLimit; },
                        IntrinsicContributionType::Minimum);
                }
            }
        }
        FlushPlannedBaseSizeIncreases(axisTracks, nAxisTracks);

        // 2. For content-based minimums.
        auto hasMinOrMaxContentMin = [](const GridTrack& t) {
            return t.minTrackSizingFunction.IsMinOrMaxContent();
        };
        for (int bi = 0; bi < batchLen; bi++) {
            GridItem* item = &batch[bi];
            if (!item->SpansTrackMatching(axis, axisTracks,
                                          hasMinOrMaxContentMin)) {
                continue;
            }
            Overflow axisOverflow = axis == AbstractAxis::Inline
                                        ? item->overflow.x
                                        : item->overflow.y;
            float space = sizer.MinContentContribution(item, axisTracks);
            int from = item->TrackRangeStart(axis);
            int count = item->TrackRangeEnd(axis) - from;
            if (space > 0.0f && count > 0) {
                if (IsScrollContainer(axisOverflow)) {
                    DistributeItemSpaceToBaseSize(
                        isFlex, useFlexFactorForDistribution, space,
                        axisTracks + from, count, hasMinOrMaxContentMin,
                        [](const GridTrack& t) {
                            return t.FitContentLimitedGrowthLimit();
                        },
                        IntrinsicContributionType::Minimum);
                } else {
                    DistributeItemSpaceToBaseSize(
                        isFlex, useFlexFactorForDistribution, space,
                        axisTracks + from, count, hasMinOrMaxContentMin,
                        [](const GridTrack& t) { return t.growthLimit; },
                        IntrinsicContributionType::Minimum);
                }
            }
        }
        FlushPlannedBaseSizeIncreases(axisTracks, nAxisTracks);

        // 3. For max-content minimums, under a max-content constraint.
        if (axisAvailableGridSpace.kind == AvailableSpace::Kind::MaxContent) {
            // A track with an auto min sizing function, excluding those whose
            // max is min-content. The exclusion matches Chrome, and follows
            // from minmax()'s "if the max is less than the min, the max is
            // floored by the min".
            auto hasAutoMin = [](const GridTrack& t) {
                return t.minTrackSizingFunction.IsAuto() &&
                       !t.maxTrackSizingFunction.IsMinContent();
            };
            auto hasMaxContentMin = [](const GridTrack& t) {
                return t.minTrackSizingFunction.IsMaxContent();
            };
            for (int bi = 0; bi < batchLen; bi++) {
                GridItem* item = &batch[bi];
                if (!item->SpansTrackMatching(
                        axis, axisTracks, [&](const GridTrack& t) {
                            return hasAutoMin(t) || hasMaxContentMin(t);
                        })) {
                    continue;
                }
                float axisMaxContentSize =
                    sizer.MaxContentContribution(item, axisTracks);
                Optf limit = SpannedTrackLimit(*item, axis, axisTracks,
                                               axisInnerNodeSize, calc);
                float space = MaybeMin(axisMaxContentSize, limit);
                // Same proportion as the intrinsic minimums above. Anchor
                // on the inflexible base sizes so this pass does not
                // compound the scaling already applied there.
                if (isFlex) {
                    int fromFlex = item->TrackRangeStart(axis);
                    int countFlex = item->TrackRangeEnd(axis) - fromFlex;
                    float inflexible = InflexibleTrackBaseSizes(
                        axisTracks + fromFlex, countFlex);
                    float scale = F32Min(
                        CrossedFlexFactorSum(axisTracks + fromFlex, countFlex),
                        1.0f);
                    space =
                        inflexible + F32Max(space - inflexible, 0.0f) * scale;
                }
                int from = item->TrackRangeStart(axis);
                int count = item->TrackRangeEnd(axis) - from;
                if (space <= 0.0f || count <= 0) {
                    continue;
                }
                bool anyMaxContentMin = false;
                for (int k = from; k < from + count; k++) {
                    if (hasMaxContentMin(axisTracks[k])) {
                        anyMaxContentMin = true;
                        break;
                    }
                }
                // Distributing to MaxContent tracks in preference to Auto ones
                // is not in the spec, but both Chrome and Firefox do it.
                // https://www.w3.org/TR/css-grid-1/#track-size-max-content-min
                if (anyMaxContentMin) {
                    DistributeItemSpaceToBaseSize(
                        isFlex, useFlexFactorForDistribution, space,
                        axisTracks + from, count, hasMaxContentMin,
                        [](const GridTrack&) { return INFINITY; },
                        IntrinsicContributionType::Maximum);
                } else {
                    DistributeItemSpaceToBaseSize(
                        isFlex, useFlexFactorForDistribution, space,
                        axisTracks + from, count, hasAutoMin,
                        [](const GridTrack& t) {
                            return t.FitContentLimitedGrowthLimit();
                        },
                        IntrinsicContributionType::Maximum);
                }
            }
            FlushPlannedBaseSizeIncreases(axisTracks, nAxisTracks);
        }

        // In all cases, keep growing tracks with a max-content min sizing
        // function to fit these items' max-content contributions.
        auto hasMaxContentMinFn = [](const GridTrack& t) {
            return t.minTrackSizingFunction.IsMaxContent();
        };
        for (int bi = 0; bi < batchLen; bi++) {
            GridItem* item = &batch[bi];
            if (!item->SpansTrackMatching(axis, axisTracks,
                                          hasMaxContentMinFn)) {
                continue;
            }
            float space = sizer.MaxContentContribution(item, axisTracks);
            int from = item->TrackRangeStart(axis);
            int count = item->TrackRangeEnd(axis) - from;
            if (space > 0.0f && count > 0) {
                DistributeItemSpaceToBaseSize(
                    isFlex, useFlexFactorForDistribution, space,
                    axisTracks + from, count, hasMaxContentMinFn,
                    [](const GridTrack& t) { return t.growthLimit; },
                    IntrinsicContributionType::Maximum);
            }
        }
        FlushPlannedBaseSizeIncreases(axisTracks, nAxisTracks);

        // 4. Any growth limit now below its base size rises to match it.
        for (int i = 0; i < nAxisTracks; i++) {
            if (axisTracks[i].growthLimit < axisTracks[i].baseSize) {
                axisTracks[i].growthLimit = axisTracks[i].baseSize;
            }
        }

        // A flexible track cannot also have an intrinsic max sizing function,
        // so steps 5 and 6 do not apply to one.
        if (!isFlex) {
            // 5. For intrinsic maximums.
            auto hasIntrinsicMax = [&](const GridTrack& t) {
                return !t.maxTrackSizingFunction
                            .HasDefiniteValue(axisInnerNodeSize);
            };
            for (int bi = 0; bi < batchLen; bi++) {
                GridItem* item = &batch[bi];
                if (!item->SpansTrackMatching(axis, axisTracks,
                                              hasIntrinsicMax)) {
                    continue;
                }
                float space = sizer.MinContentContribution(item, axisTracks);
                int from = item->TrackRangeStart(axis);
                int count = item->TrackRangeEnd(axis) - from;
                if (space > 0.0f && count > 0) {
                    DistributeItemSpaceToGrowthLimit(space, axisTracks + from,
                                                     count, hasIntrinsicMax);
                }
            }
            // Tracks whose growth limit went from infinite to finite here are
            // marked infinitely growable for the next step.
            FlushPlannedGrowthLimitIncreases(axisTracks, nAxisTracks, true);

            // 6. For max-content maximums, limiting fit-content() tracks by
            //    their argument.
            auto hasMaxContentMax = [&](const GridTrack& t) {
                return t.maxTrackSizingFunction.IsMaxContentAlike() ||
                       (t.maxTrackSizingFunction.UsesPercentage() &&
                        !IsSome(axisInnerNodeSize));
            };
            for (int bi = 0; bi < batchLen; bi++) {
                GridItem* item = &batch[bi];
                if (!item->SpansTrackMatching(axis, axisTracks,
                                              hasMaxContentMax)) {
                    continue;
                }
                float space = sizer.MaxContentContribution(item, axisTracks);
                int from = item->TrackRangeStart(axis);
                int count = item->TrackRangeEnd(axis) - from;
                if (space > 0.0f && count > 0) {
                    DistributeItemSpaceToGrowthLimit(space, axisTracks + from,
                                                     count, hasMaxContentMax);
                }
            }
            FlushPlannedGrowthLimitIncreases(axisTracks, nAxisTracks, false);
        }
    }

    // Step 5. Any track still with an infinite growth limit — because nothing
    // was placed in it, or because it is flexible — takes its base size.
    // Important: without this the maximise-tracks step would affect flexible
    // tracks.
    for (int i = 0; i < nAxisTracks; i++) {
        if (axisTracks[i].growthLimit == INFINITY) {
            axisTracks[i].growthLimit = axisTracks[i].baseSize;
        }
    }
}

// 11.6 Maximise Tracks: distribute free space to tracks with finite growth
// limits, up to those limits.
void MaximiseTracks(GridTrack* axisTracks, int n,
                    AvailableSpace axisAvailableGridSpace) {
    float usedSpace = 0.0f;
    for (int i = 0; i < n; i++) {
        usedSpace += axisTracks[i].baseSize;
    }
    float freeSpace = axisAvailableGridSpace.ComputeFreeSpace(usedSpace);
    if (freeSpace == INFINITY) {
        for (int i = 0; i < n; i++) {
            axisTracks[i].baseSize = axisTracks[i].growthLimit;
        }
    } else if (freeSpace > 0.0f) {
        DistributeSpaceUpToLimits(
            freeSpace, axisTracks, n, [](const GridTrack&) { return true; },
            [](const GridTrack&) { return 1.0f; },
            [](const GridTrack& t) { return t.baseSize; },
            [](const GridTrack& t) {
                return t.FitContentLimitedGrowthLimit();
            });
        for (int i = 0; i < n; i++) {
            axisTracks[i].baseSize += axisTracks[i].itemIncurredIncrease;
            axisTracks[i].itemIncurredIncrease = 0.0f;
        }
    }
}

// 11.7.1 Find the Size of an fr: the largest an fr can be without exceeding
// the target size.
float FindSizeOfFr(const GridTrack* tracks, int n, float spaceToFill) {
    // The trivial case, which also keeps the loop below from spinning.
    if (spaceToFill == 0.0f) {
        return 0.0f;
    }

    // A flexible track whose flex factor times the hypothetical fr size is
    // below its base size has to be treated as inflexible, and the algorithm
    // restarted. Starting at infinity makes that impossible on the first pass.
    float hypotheticalFrSize = INFINITY;
    float previousIterHypotheticalFrSize;
    // Every restart treats one more flexible track as inflexible, so a valid
    // hypothetical fr size is found within n + 1 iterations. Non-finite
    // values make comparisons false, so the bound guarantees termination.
    int maxIterations = n + 1;
    for (int iter = 0; iter < maxIterations; iter++) {
        float usedSpace = 0.0f;
        float naiveFlexFactorSum = 0.0f;
        for (int i = 0; i < n; i++) {
            const GridTrack& t = tracks[i];
            if (t.maxTrackSizingFunction.IsFr() &&
                t.maxTrackSizingFunction.raw.Value() * hypotheticalFrSize >=
                    t.baseSize) {
                naiveFlexFactorSum += t.maxTrackSizingFunction.raw.Value();
            } else {
                usedSpace += t.baseSize;
            }
        }
        float leftoverSpace = spaceToFill - usedSpace;
        float flexFactor = F32Max(naiveFlexFactorSum, 1.0f);

        previousIterHypotheticalFrSize = hypotheticalFrSize;
        hypotheticalFrSize = leftoverSpace / flexFactor;

        bool valid = true;
        for (int i = 0; i < n; i++) {
            const GridTrack& t = tracks[i];
            if (!t.maxTrackSizingFunction.IsFr()) {
                continue;
            }
            float ff = t.maxTrackSizingFunction.raw.Value();
            if (!(ff * hypotheticalFrSize >= t.baseSize ||
                  ff * previousIterHypotheticalFrSize < t.baseSize)) {
                valid = false;
                break;
            }
        }
        if (valid) {
            break;
        }
    }
    return hypotheticalFrSize;
}

// 11.7 Expand Flexible Tracks
void ExpandFlexibleTracks(TaffyTree* tree, AbstractAxis axis,
                          GridTrack* axisTracks, int nAxisTracks,
                          const GridTrack* otherAxisTracks, GridItem* items,
                          int nItems, Optf axisMinSize, Optf axisMaxSize,
                          AvailableSpace axisAvailableSpaceForExpansion,
                          SizeFOpt innerNodeSize, TrackSizeEstimate estimate) {
    float flexFraction = 0.0f;
    if (axisAvailableSpaceForExpansion.kind == AvailableSpace::Kind::Definite) {
        float availableSpace = axisAvailableSpaceForExpansion.value;
        float usedSpace = 0.0f;
        for (int i = 0; i < nAxisTracks; i++) {
            usedSpace += axisTracks[i].baseSize;
        }
        float freeSpace = availableSpace - usedSpace;
        flexFraction = freeSpace <= 0.0f ? 0.0f
                                         : FindSizeOfFr(axisTracks, nAxisTracks,
                                                        availableSpace);
    } else if (axisAvailableSpaceForExpansion
                   .kind == AvailableSpace::Kind::MinContent) {
        // Under a min-content constraint the used flex fraction is zero.
        flexFraction = 0.0f;
    } else {
        // Indefinite free space: the maximum of the per-track fractions and
        // the per-item ones.
        float trackMax = 0.0f;
        for (int i = 0; i < nAxisTracks; i++) {
            const GridTrack& t = axisTracks[i];
            if (!t.maxTrackSizingFunction.IsFr()) {
                continue;
            }
            float ff = t.FlexFactor();
            float v = ff > 1.0f ? t.baseSize / ff : t.baseSize;
            trackMax = F32Max(trackMax, v);
        }
        float itemMax = 0.0f;
        IntrinsicSizeMeasurer sizer;
        sizer.tree = tree;
        sizer.otherAxisTracks = otherAxisTracks;
        sizer.estimate = estimate;
        sizer.axis = axis;
        sizer.innerNodeSize = innerNodeSize;
        for (int i = 0; i < nItems; i++) {
            GridItem* item = &items[i];
            if (!item->CrossesFlexibleTrack(axis)) {
                continue;
            }
            int from = item->TrackRangeStart(axis);
            int count = item->TrackRangeEnd(axis) - from;
            // The other axis is already sized, so the contribution wraps
            // against that grid area rather than against an infinite width.
            float maxContentContribution =
                sizer.MaxContentContribution(item, axisTracks);
            itemMax = F32Max(itemMax, FindSizeOfFr(axisTracks + from, count,
                                                   maxContentContribution));
        }
        flexFraction = F32Max(trackMax, itemMax);

        // If this fraction would make the grid smaller than its min size (or
        // bigger than its max size), redo the step treating the free space as
        // definite and equal to that size. min takes precedence over max.
        float hypotheticalGridSize = 0.0f;
        for (int i = 0; i < nAxisTracks; i++) {
            const GridTrack& t = axisTracks[i];
            if (t.maxTrackSizingFunction.IsFr()) {
                hypotheticalGridSize +=
                    F32Max(t.baseSize,
                           t.maxTrackSizingFunction.raw.Value() * flexFraction);
            } else {
                hypotheticalGridSize += t.baseSize;
            }
        }
        float minSize = UnwrapOr(axisMinSize, 0.0f);
        float maxSize = UnwrapOr(axisMaxSize, INFINITY);
        if (hypotheticalGridSize < minSize) {
            flexFraction = FindSizeOfFr(axisTracks, nAxisTracks, minSize);
        } else if (hypotheticalGridSize > maxSize) {
            flexFraction = FindSizeOfFr(axisTracks, nAxisTracks, maxSize);
        }
    }

    for (int i = 0; i < nAxisTracks; i++) {
        GridTrack& t = axisTracks[i];
        if (!t.maxTrackSizingFunction.IsFr()) {
            continue;
        }
        t.baseSize = F32Max(
            t.baseSize, t.maxTrackSizingFunction.raw.Value() * flexFraction);
    }
}

// 11.8 Stretch auto Tracks
void StretchAutoTracks(GridTrack* axisTracks, int n, Optf axisMinSize,
                       AvailableSpace axisAvailableSpaceForExpansion) {
    int numAutoTracks = 0;
    for (int i = 0; i < n; i++) {
        if (axisTracks[i].maxTrackSizingFunction.IsAuto()) {
            numAutoTracks++;
        }
    }
    if (numAutoTracks == 0) {
        return;
    }
    float usedSpace = 0.0f;
    for (int i = 0; i < n; i++) {
        usedSpace += axisTracks[i].baseSize;
    }
    // With indefinite free space but a definite min size, that min size is
    // what the free space is computed from.
    float freeSpace;
    if (axisAvailableSpaceForExpansion.IsDefinite()) {
        freeSpace = axisAvailableSpaceForExpansion.ComputeFreeSpace(usedSpace);
    } else {
        freeSpace = IsSome(axisMinSize) ? axisMinSize - usedSpace : 0.0f;
    }
    if (freeSpace > 0.0f) {
        float extra = freeSpace / (float)numAutoTracks;
        for (int i = 0; i < n; i++) {
            if (axisTracks[i].maxTrackSizingFunction.IsAuto()) {
                axisTracks[i].baseSize += extra;
            }
        }
    }
}

// The track sizing algorithm. Gutters are treated as empty fixed-size tracks.
void TrackSizingAlgorithm(TaffyTree* tree, AbstractAxis axis, Optf axisMinSize,
                          Optf axisMaxSize, AlignContent axisAlignment,
                          AlignContent otherAxisAlignment,
                          SizeAvail availableGridSpace, SizeFOpt innerNodeSize,
                          GridTrack* axisTracks, int nAxisTracks,
                          GridTrack* otherAxisTracks, int nOtherAxisTracks,
                          GridItem* items, int nItems,
                          TrackSizeEstimate estimate,
                          bool hasBaselineAlignedItem) {
    // 11.4 Initialise Track sizes.
    Optf percentageBasis = Or(Get(innerNodeSize, axis), axisMinSize);
    InitializeTrackSizes(tree, axisTracks, nAxisTracks, percentageBasis);

    // fit-content() limits are fixed for the rest of this pass. A percentage
    // or calc() limit stays infinite while the container's inner size is not.
    Optf axisInnerNodeSize = Get(innerNodeSize, axis);
    for (int i = 0; i < nAxisTracks; i++) {
        axisTracks[i].ResolveFitContentLimit(axisInnerNodeSize, tree->calc);
    }

    // 11.5.1 Shim item baselines.
    if (hasBaselineAlignedItem) {
        ResolveItemBaselines(tree, axis, items, nItems, innerNodeSize);
    }

    // A fixed min that already equals its growth limit is final. An
    // intrinsic min can still grow past a fixed max, as minmax(auto, 0px)
    // does, so those tracks are not skipped.
    bool allAtLimit = true;
    for (int i = 0; i < nAxisTracks; i++) {
        const GridTrack& t = axisTracks[i];
        if (t.baseSize != t.growthLimit ||
            !IsSome(t.minTrackSizingFunction
                        .DefiniteValue(percentageBasis, tree->calc))) {
            allAtLimit = false;
            break;
        }
    }
    if (allAtLimit) {
        return;
    }

    // The extra amount added to each spanned gutter when estimating an item's
    // size in the opposite axis.
    float gutterAlignmentAdjustment = ComputeAlignmentGutterAdjustment(
        otherAxisAlignment, Get(innerNodeSize, Other(axis)), otherAxisTracks,
        nOtherAxisTracks, estimate, tree->calc);
    if (nOtherAxisTracks > 3) {
        for (int i = 2; i < nOtherAxisTracks; i += 2) {
            otherAxisTracks[i]
                .contentAlignmentAdjustment = gutterAlignmentAdjustment;
        }
    }

    // 11.5 Resolve Intrinsic Track Sizes.
    ResolveIntrinsicTrackSizes(
        tree, axis, axisTracks, nAxisTracks, otherAxisTracks, items, nItems,
        availableGridSpace.Get(axis), innerNodeSize, estimate);

    // 11.6 Maximise Tracks.
    MaximiseTracks(axisTracks, nAxisTracks, availableGridSpace.Get(axis));

    // The last two expansion steps should only expand into space the grid
    // container's own size generates, not any available space, so definite
    // available space is mapped to max-content when the inner node size is
    // unknown.
    AvailableSpace axisAvailableSpaceForExpansion;
    Optf innerAxis = Get(innerNodeSize, axis);
    if (IsSome(innerAxis)) {
        axisAvailableSpaceForExpansion = AvailableSpace::Definite(innerAxis);
    } else if (availableGridSpace.Get(axis)
                   .kind == AvailableSpace::Kind::MinContent) {
        axisAvailableSpaceForExpansion = AvailableSpace::MinContent();
    } else {
        axisAvailableSpaceForExpansion = AvailableSpace::MaxContent();
    }

    // 11.7 Expand Flexible Tracks.
    ExpandFlexibleTracks(tree, axis, axisTracks, nAxisTracks, otherAxisTracks,
                         items, nItems, axisMinSize, axisMaxSize,
                         axisAvailableSpaceForExpansion, innerNodeSize,
                         estimate);

    // 11.8 Stretch auto Tracks.
    if (axisAlignment.keyword == AlignContentKeyword::Stretch) {
        StretchAutoTracks(axisTracks, nAxisTracks, axisMinSize,
                          axisAvailableSpaceForExpansion);
    }
}

// ─── alignment.rs ────────────────────────────────────────────────────────

// Align the tracks within the grid per align-content (rows) or
// justify-content (columns). Only does anything when the grid is a different
// size from the container in that axis.
void AlignTracks(float gridContainerContentBoxSize, LineF padding, LineF border,
                 GridTrack* tracks, int n, AlignContent trackAlignmentStyle,
                 bool axisIsReversed) {
    float usedSize = 0.0f;
    for (int i = 0; i < n; i++) {
        usedSize += tracks[i].baseSize;
    }
    float freeSpace = gridContainerContentBoxSize - usedSize;
    float origin = padding.start + border.start;

    int numTracks = 0;
    for (int i = 1; i < n; i += 2) {
        if (!tracks[i].isCollapsed) {
            numTracks++;
        }
    }

    // Grid treats gaps as full tracks rather than applying them at alignment,
    // so the gap is zero here, and grid layout is never reversed.
    float gap = 0.0f;
    bool layoutIsReversed = false;
    AlignContentKeyword trackAlignment =
        ApplyAlignmentFallback(freeSpace, numTracks, trackAlignmentStyle);
    if (axisIsReversed) {
        trackAlignment = Reversed(trackAlignment);
    }

    float emptyGridOffset =
        numTracks == 0
            ? ComputeAlignmentOffset(freeSpace, numTracks, gap, trackAlignment,
                                     layoutIsReversed, true)
            : 0.0f;
    float totalOffset = origin + emptyGridOffset;
    bool seenNonCollapsedTrack = false;
    // Tracks stay in logical order. RTL assigns physical offsets from the
    // inline-end by walking the vector backwards. Gutters are still the
    // even slots of that walk, because the vector both starts and ends on
    // a gutter.
    auto positionTrack = [&](int i, GridTrack& track) {
        bool isGutter = (i % 2) == 0;
        bool isNonCollapsedTrack = !isGutter && !track.isCollapsed;
        bool isFirst = isNonCollapsedTrack && !seenNonCollapsedTrack;

        float offset = isNonCollapsedTrack
                           ? ComputeAlignmentOffset(freeSpace, numTracks, gap,
                                                    trackAlignment,
                                                    layoutIsReversed, isFirst)
                           : 0.0f;
        track.offset = totalOffset + offset;
        totalOffset = totalOffset + offset + track.baseSize;
        if (isNonCollapsedTrack) {
            seenNonCollapsedTrack = true;
        }
    };
    if (axisIsReversed) {
        for (int i = 0; i < n; i++) {
            positionTrack(i, tracks[n - 1 - i]);
        }
    } else {
        for (int i = 0; i < n; i++) {
            positionTrack(i, tracks[i]);
        }
    }
}

struct AlignedAxis {
    float start = 0.0f;
    LineF margin;
};

// Align and size a grid item along one axis.
AlignedAxis AlignItemWithinArea(LineF gridArea, AlignSelf alignmentStyle,
                                float resolvedSize, Position position,
                                RectFOpt insetLine, bool vertical,
                                RectFOpt marginLine, float baselineShim,
                                Direction direction) {
    Optf insetStart = vertical ? insetLine.top : insetLine.left;
    Optf insetEnd = vertical ? insetLine.bottom : insetLine.right;
    Optf marginStart = vertical ? marginLine.top : marginLine.left;
    Optf marginEnd = vertical ? marginLine.bottom : marginLine.right;

    LineF nonAutoMargin = {UnwrapOr(marginStart, 0.0f) + baselineShim,
                           UnwrapOr(marginEnd, 0.0f)};
    float gridAreaSize = F32Max(gridArea.end - gridArea.start, 0.0f);
    float freeSpace =
        F32Max(gridAreaSize - resolvedSize - nonAutoMargin.Sum(), 0.0f);

    int autoMarginCount =
        (IsSome(marginStart) ? 0 : 1) + (IsSome(marginEnd) ? 0 : 1);
    float autoMarginSize =
        autoMarginCount > 0 ? freeSpace / (float)autoMarginCount : 0.0f;
    LineF resolvedMargin = {
        UnwrapOr(marginStart, autoMarginSize) + baselineShim,
        UnwrapOr(marginEnd, autoMarginSize)};

    bool overflows = resolvedSize + nonAutoMargin.Sum() > gridAreaSize;
    AlignItemsKeyword keyword =
        ResolveSelfAlignmentSafety(alignmentStyle, overflows);

    float alignmentBasedOffset;
    switch (keyword) {
        case AlignItemsKeyword::End:
        case AlignItemsKeyword::FlexEnd:
            alignmentBasedOffset =
                IsRtl(direction)
                    ? resolvedMargin.start
                    : gridAreaSize - resolvedSize - resolvedMargin.end;
            break;
        case AlignItemsKeyword::Center:
            alignmentBasedOffset = (gridAreaSize - resolvedSize +
                                    resolvedMargin.start - resolvedMargin.end) /
                                   2.0f;
            break;
        default:
            // Start, FlexStart, Baseline and Stretch. Baseline alignment is
            // not supported here yet and is treated as start.
            alignmentBasedOffset =
                IsRtl(direction)
                    ? gridAreaSize - resolvedSize - resolvedMargin.end
                    : resolvedMargin.start;
            break;
    }

    float offsetWithinArea = alignmentBasedOffset;
    if (IsOutOfFlow(position)) {
        if (IsSome(insetStart) && IsSome(insetEnd)) {
            offsetWithinArea =
                IsRtl(direction)
                    ? gridAreaSize - insetEnd - resolvedSize - nonAutoMargin.end
                    : insetStart + nonAutoMargin.start;
        } else if (IsSome(insetStart)) {
            offsetWithinArea = insetStart + nonAutoMargin.start;
        } else if (IsSome(insetEnd)) {
            offsetWithinArea =
                gridAreaSize - insetEnd - resolvedSize - nonAutoMargin.end;
        }
    }

    float start = gridArea.start + offsetWithinArea;
    if (position == Position::Relative) {
        Optf negEnd = insetEnd;
        if (IsSome(negEnd)) {
            negEnd = -negEnd;
        }
        Optf relativeInset =
            IsRtl(direction) ? Or(negEnd, insetStart) : Or(insetStart, negEnd);
        start += UnwrapOr(relativeInset, 0.0f);
    }
    return {start, resolvedMargin};
}

struct AlignedItem {
    RectF overflowContribution;
    float yPosition = 0.0f;
    float height = 0.0f;
    int oofStart = 0;
    int oofCount = 0;
};

// Align and size a grid item into its final position.
AlignedItem AlignAndPositionItem(
    TaffyTree* tree, NodeId node, uint32_t order, RectF gridArea,
    AlignItems containerJustifyItems, AlignItems containerAlignItems,
    float baselineShim, Direction direction, float containerBorderBoxWidth,
    RectF containerBorder, bool containerIsScrollContainer,
    Vec<OofCandidate>* oofPool) {
    CalcResolver calc = tree->calc;
    SizeF gridAreaSize = {gridArea.right - gridArea.left,
                          gridArea.bottom - gridArea.top};

    const Style& style = tree->GetStyle(node);
    PointOverflow overflow = style.overflow;
    Contain contain = style.contain;
    float scrollbarWidth = style.scrollbarWidth;
    Optf aspectRatio = style.aspectRatio;
    // none takes the container's justify-items / align-items, then
    // self-start and self-end resolve against the item's own direction.
    AlignItems justifySelf =
        ResolveSelfRelative(style.justifySelf.UnwrapOr(containerJustifyItems),
                            style.direction, direction, true);
    AlignItems alignSelf =
        ResolveSelfRelative(style.alignSelf.UnwrapOr(containerAlignItems),
                            style.direction, direction, false);
    Position position = style.position;

    RectFOpt inset = style.inset
                         .MaybeResolveZip(AsOptional(gridAreaSize), calc);
    RectF padding = style.padding.ResolveOrZero(Some(gridAreaSize.w), calc);
    RectF border = style.border.ResolveOrZero(Some(gridAreaSize.w), calc);
    SizeF paddingBorderSize = (padding + border).SumAxes();
    SizeF boxSizingAdjustment = style.boxSizing == BoxSizing::ContentBox
                                    ? paddingBorderSize
                                    : SizeF::Zero();

    SizeFOpt gridAreaOpt = AsOptional(gridAreaSize);
    SizeDim sizeStyle = style.size;
    SizeFOpt inherentSize =
        MaybeAdd(MaybeApplyAspectRatio(
                     sizeStyle.MaybeResolve(gridAreaOpt, calc), aspectRatio),
                 boxSizingAdjustment);
    SizeFOpt minSize = MaybeApplyAspectRatio(
        MaybeMax(Or(MaybeAdd(style.minSize.MaybeResolve(gridAreaOpt, calc),
                             boxSizingAdjustment),
                    AsOptional(paddingBorderSize)),
                 paddingBorderSize),
        aspectRatio);
    SizeFOpt maxSize = MaybeAdd(
        MaybeApplyAspectRatio(style.maxSize.MaybeResolve(gridAreaOpt, calc),
                              aspectRatio),
        boxSizingAdjustment);

    // normal (the default when neither the parent nor the item sets another
    // keyword) stretches, unless this axis already has a preferred size.
    // An item with a preferred aspect ratio but no width or height stretches
    // its width and derives its height from the ratio.
    // https://www.w3.org/TR/css-grid-1/#grid-item-sizing
    AlignItems horizontalAlignment = justifySelf;
    if (justifySelf.keyword == AlignItemsKeyword::Normal) {
        horizontalAlignment =
            (IsSome(inherentSize.w) || sizeStyle.width.IsSizingKeyword())
                ? AlignItems{AlignItemsKeyword::Start}
                : AlignItems{AlignItemsKeyword::Stretch};
    }
    AlignItems verticalAlignment = alignSelf;
    if (alignSelf.keyword == AlignItemsKeyword::Normal) {
        verticalAlignment =
            (IsSome(inherentSize.h) || sizeStyle.height.IsSizingKeyword() ||
             IsSome(aspectRatio))
                ? AlignItems{AlignItemsKeyword::Start}
                : AlignItems{AlignItemsKeyword::Stretch};
    }

    // Not a bug: the CSS spec has both horizontal and vertical margins resolve
    // against the *width* of the grid area.
    RectFOpt margin = style.margin.MaybeResolve(Some(gridAreaSize.w), calc);

    SizeF gridAreaMinusItemMarginsSize = {
        F32Max(MaybeSub(MaybeSub(gridAreaSize.w, margin.left), margin.right),
               0.0f),
        F32Max(MaybeSub(MaybeSub(gridAreaSize.h, margin.top), margin.bottom) -
                   baselineShim,
               0.0f)};

    SizingKeywordResolution keywordWidth;
    if (!IsSome(inherentSize.w)) {
        keywordWidth = ResolveSizingKeyword(
            sizeStyle.width, Some(gridAreaMinusItemMarginsSize.w),
            Some(gridAreaSize.w), calc);
    }
    SizingKeywordResolution keywordHeight;
    if (!IsSome(inherentSize.h)) {
        keywordHeight = ResolveSizingKeyword(
            sizeStyle.height, Some(gridAreaMinusItemMarginsSize.h),
            Some(gridAreaSize.h), calc);
    }
    // One measure covers both axes. An absolute item is measured later, once
    // its insets have had a chance to fill a missing axis.
    SizeFOpt keywordMeasured = SizeFOptNone();
    if (!IsOutOfFlow(position) && keywordWidth.isSome && keywordHeight.isSome &&
        keywordWidth.kind == SizingKeywordKind::Measure &&
        keywordHeight.kind == SizingKeywordKind::Measure) {
        keywordMeasured = AsOptional(tree->MeasureChildSizeBoth(
            node, SizeFOptNone(), gridAreaOpt,
            {keywordWidth.measure, keywordHeight.measure},
            SizingMode::InherentSize, LineBool::False()));
    }

    Optf width = inherentSize.w;
    if (!IsSome(width)) {
        if (IsOutOfFlow(position) && IsSome(inset.left) &&
            IsSome(inset.right)) {
            width = Some(F32Max(
                gridAreaMinusItemMarginsSize.w - inset.left - inset.right,
                0.0f));
        } else if (keywordWidth.isSome) {
            if (keywordWidth.kind == SizingKeywordKind::Exact) {
                width = Some(keywordWidth.exact);
            } else if (IsSome(keywordMeasured.w)) {
                width = keywordMeasured.w;
            } else {
                width = Some(tree->MeasureChildSize(
                    node, SizeFOptNone(), gridAreaOpt,
                    {keywordWidth.measure,
                     AvailableSpace::Definite(gridAreaMinusItemMarginsSize.h)},
                    SizingMode::InherentSize, AbsoluteAxis::Horizontal,
                    LineBool::False()));
            }
        } else if (IsSome(margin.left) && IsSome(margin.right) &&
                   horizontalAlignment.keyword == AlignItemsKeyword::Stretch &&
                   !IsOutOfFlow(position)) {
            width = Some(gridAreaMinusItemMarginsSize.w);
        }
    }
    SizeFOpt sized =
        MaybeApplyAspectRatio(SizeFOpt{width, inherentSize.h}, aspectRatio);

    Optf height = sized.h;
    if (!IsSome(height)) {
        if (IsOutOfFlow(position) && IsSome(inset.top) &&
            IsSome(inset.bottom)) {
            height = Some(F32Max(
                gridAreaMinusItemMarginsSize.h - inset.top - inset.bottom,
                0.0f));
        } else if (keywordHeight.isSome) {
            if (keywordHeight.kind == SizingKeywordKind::Exact) {
                height = Some(keywordHeight.exact);
            } else if (IsSome(keywordMeasured.h)) {
                height = keywordMeasured.h;
            } else {
                AvailableSpace widthAvail =
                    IsSome(width) ? AvailableSpace::Definite(width)
                                  : AvailableSpace::Definite(
                                        gridAreaMinusItemMarginsSize.w);
                height = Some(tree->MeasureChildSize(
                    node, {width, None()}, gridAreaOpt,
                    {widthAvail, keywordHeight.measure},
                    SizingMode::InherentSize, AbsoluteAxis::Vertical,
                    LineBool::False()));
            }
        } else if (IsSome(margin.top) && IsSome(margin.bottom) &&
                   verticalAlignment.keyword == AlignItemsKeyword::Stretch &&
                   !IsOutOfFlow(position)) {
            height = Some(gridAreaMinusItemMarginsSize.h);
        }
    }
    sized = MaybeApplyAspectRatio(SizeFOpt{sized.w, height}, aspectRatio);
    sized = MaybeClamp(sized, minSize, maxSize);

    SizeAvail avail = SizeAvail::Definite(gridAreaMinusItemMarginsSize);
    SizeFOpt size = sized;
    if (IsOutOfFlow(position) && (!IsSome(sized.w) || !IsSome(sized.h))) {
        SizeF measured = tree->MeasureChildSizeBoth(
            node, sized, gridAreaOpt, avail, SizingMode::InherentSize,
            LineBool::False());
        size = AsOptional(measured);
    }

    LayoutOutput layoutOutput =
        tree->PerformChildLayout(node, size, gridAreaOpt, avail,
                                 SizingMode::InherentSize, LineBool::False());

    SizeF finalSize =
        MaybeClamp(UnwrapOr(size, layoutOutput.size), minSize, maxSize);

    AlignedAxis xr = AlignItemWithinArea(
        {gridArea.left, gridArea.right}, horizontalAlignment, finalSize.w,
        position, inset, false, margin, 0.0f, direction);
    AlignedAxis yr = AlignItemWithinArea(
        {gridArea.top, gridArea.bottom}, verticalAlignment, finalSize.h,
        position, inset, true, margin, baselineShim, Direction::Ltr);

    SizeF scrollbarSize = {
        overflow.y == Overflow::Scroll ? scrollbarWidth : 0.0f,
        overflow.x == Overflow::Scroll ? scrollbarWidth : 0.0f};

    Layout layout;
    layout.order = order;
    layout.location = {xr.start, yr.start};
    layout.size = finalSize;
    layout.scrollableOverflowRect = layoutOutput.scrollableOverflowRect;
    layout.scrollbarSize = scrollbarSize;
    layout.padding = padding;
    layout.border = border;
    layout.margin = {xr.margin.start, xr.margin.end, yr.margin.start,
                     yr.margin.end};
    tree->SetUnroundedLayout(node, layout);

    RectF contribution = ComputeScrollableOverflowContribution(
        {IsRtl(direction) ? containerBorderBoxWidth - (xr.start + finalSize.w) -
                                containerBorder.right
                          : xr.start - containerBorder.left,
         yr.start - containerBorder.top},
        finalSize, layoutOutput.scrollableOverflowRect, overflow, contain,
        containerIsScrollContainer);
    OofCandRange range;
    OofAppendRange(oofPool, &range, layoutOutput.oofCandidates);
    if (range.count > 0) {
        OofTranslateRange(oofPool, range, {xr.start, yr.start});
    }
    return {contribution, yr.start, finalSize.h, range.start, range.count};
}

// ─── mod.rs ──────────────────────────────────────────────────────────────

// Track start/end positions for the out-of-flow pass. Only real tracks are
// stored; a gutter is the gap between them. An empty axis stores nothing
// and the pass falls back to the padding box.
void StoreGridOofSnapshot(TaffyTree* tree, NodeId node, uint16_t colAuto,
                          uint16_t rowAuto, TrackCounts colCounts,
                          TrackCounts rowCounts, const Vec<GridTrack>& columns,
                          const Vec<GridTrack>& rows) {
    NodeData* d = tree->Get(node);
    if (!d) {
        return;
    }
    auto fill = [](GridAxisPositions* axis, uint16_t autoRep,
                   TrackCounts counts, const Vec<GridTrack>& tracks) {
        axis->negativeImplicit = counts.negativeImplicit;
        axis->explicitCount = counts.explicitCount;
        axis->positiveImplicit = counts.positiveImplicit;
        axis->autoRepetitions = autoRep;
        for (int i = 0; i < len(tracks); i++) {
            if (tracks[i].kind != GridTrackKind::Track) {
                continue;
            }
            if (tracks[i].isCollapsed) {
                int index = len(axis->spans);
                if (len(axis->collapsed) > 0 &&
                    axis->collapsed[len(axis->collapsed) - 1].end == index) {
                    axis->collapsed[len(axis->collapsed) - 1].end = index + 1;
                } else {
                    CollapsedRun run;
                    run.start = index;
                    run.end = index + 1;
                    VecAppend(axis->collapsed, run);
                }
            }
            GridTrackSpan span;
            span.start = tracks[i].offset;
            span.end = tracks[i].offset + tracks[i].baseSize;
            VecAppend(axis->spans, span);
        }
    };
    d->gridOof.Clear();
    d->gridOof.live = true;
    fill(&d->gridOof.columns, colAuto, colCounts, columns);
    fill(&d->gridOof.rows, rowAuto, rowCounts, rows);
}

} // namespace

// The CSS Grid layout algorithm, in four phases: resolve the explicit grid,
// place the items (which resolves the implicit grid), size the tracks, then
// align and place.
LayoutOutput ComputeGridLayout(TaffyTree* tree, NodeId node,
                               const LayoutInput& inputs) {
    CalcResolver calc = tree->calc;
    SizeFOpt knownDimensions = inputs.knownDimensions;
    SizeFOpt parentSize = inputs.parentSize;
    SizeAvail availableSpace = inputs.availableSpace;
    RunMode runMode = inputs.runMode;

    const Style& style = tree->GetStyle(node);
    Direction direction = style.direction;
    Contain contain = style.contain;

    // 1. Available grid space.
    // https://www.w3.org/TR/css-grid-1/#available-grid-space
    Optf aspectRatio = style.aspectRatio;
    RectF padding = style.padding.ResolveOrZero(parentSize.w, calc);
    RectF border = style.border.ResolveOrZero(parentSize.w, calc);
    RectF paddingBorder = padding + border;
    SizeF paddingBorderSize = paddingBorder.SumAxes();
    SizeF boxSizingAdjustment = style.boxSizing == BoxSizing::ContentBox
                                    ? paddingBorderSize
                                    : SizeF::Zero();

    SizeFOpt minSize =
        MaybeAdd(MaybeApplyAspectRatio(
                     style.minSize.MaybeResolve(parentSize, calc), aspectRatio),
                 boxSizingAdjustment);
    SizeFOpt maxSize =
        MaybeAdd(MaybeApplyAspectRatio(
                     style.maxSize.MaybeResolve(parentSize, calc), aspectRatio),
                 boxSizingAdjustment);
    SizeFOpt preferredSize = SizeFOptNone();
    if (inputs.sizingMode == SizingMode::InherentSize) {
        preferredSize = MaybeAdd(
            MaybeApplyAspectRatio(style.size.MaybeResolve(parentSize, calc),
                                  aspectRatio),
            boxSizingAdjustment);
    }

    // A node that scrolls vertically needs *horizontal* space reserved for its
    // scrollbar, hence the transposed axes.
    PointOverflow t = style.overflow.Transpose();
    bool isScrollContainer = IsScrollContainer(style.overflow.x) ||
                             IsScrollContainer(style.overflow.y);
    PointF scrollbarGutter = {
        t.x == Overflow::Scroll ? style.scrollbarWidth : 0.0f,
        t.y == Overflow::Scroll ? style.scrollbarWidth : 0.0f};
    RectF contentBoxInset = paddingBorder;
    contentBoxInset.bottom += scrollbarGutter.y;
    if (direction == Direction::Ltr) {
        contentBoxInset.right += scrollbarGutter.x;
    } else {
        contentBoxInset.left += scrollbarGutter.x;
    }

    // normal behaves as stretch for a grid container.
    AlignContent alignContent =
        style.alignContent.keyword == AlignContentKeyword::Normal
            ? AlignContent{AlignContentKeyword::Stretch}
            : style.alignContent;
    AlignContent justifyContent =
        style.justifyContent.keyword == AlignContentKeyword::Normal
            ? AlignContent{AlignContentKeyword::Stretch}
            : style.justifyContent;
    AlignItems alignItems = style.alignItems;
    AlignItems justifyItems = style.justifyItems;

    SizeFOpt sizeOrPreferred = Or(knownDimensions, preferredSize);
    // The available space the grid is sized against: a known or preferred
    // size wins over what the parent offered, then min/max clamp it and the
    // padding+border floor it.
    SizeAvail constrainedAvailableSpace = availableSpace;
    if (IsSome(sizeOrPreferred.w)) {
        constrainedAvailableSpace
            .width = AvailableSpace::Definite(sizeOrPreferred.w);
    }
    if (IsSome(sizeOrPreferred.h)) {
        constrainedAvailableSpace
            .height = AvailableSpace::Definite(sizeOrPreferred.h);
    }
    constrainedAvailableSpace =
        MaybeClamp(constrainedAvailableSpace, minSize, maxSize);
    constrainedAvailableSpace
        .width = MaybeMax(constrainedAvailableSpace.width, paddingBorderSize.w);
    constrainedAvailableSpace.height =
        MaybeMax(constrainedAvailableSpace.height, paddingBorderSize.h);

    SizeAvail availableGridSpace = constrainedAvailableSpace;
    if (availableGridSpace.width.IsDefinite()) {
        availableGridSpace.width =
            AvailableSpace::Definite(availableGridSpace.width.value -
                                     contentBoxInset.HorizontalAxisSum());
    }
    if (availableGridSpace.height.IsDefinite()) {
        availableGridSpace.height =
            AvailableSpace::Definite(availableGridSpace.height.value -
                                     contentBoxInset.VerticalAxisSum());
    }

    SizeFOpt outerNodeSize = MaybeMax(
        MaybeClamp(sizeOrPreferred, minSize, maxSize), paddingBorderSize);
    SizeFOpt innerNodeSize = {
        MaybeSub(outerNodeSize.w, contentBoxInset.HorizontalAxisSum()),
        MaybeSub(outerNodeSize.h, contentBoxInset.VerticalAxisSum())};
    SizeFOpt innerMinSize = MaybeSub(minSize, contentBoxInset.SumAxes());
    SizeFOpt innerMaxSize = MaybeSub(maxSize, contentBoxInset.SumAxes());

    if (runMode == RunMode::ComputeSize) {
        if (BothAxisDefined(outerNodeSize)) {
            return LayoutOutput::FromOuterSize(
                {outerNodeSize.w, outerNodeSize.h});
        }
        if (inputs.axis == RequestedAxis::Horizontal &&
            IsSome(outerNodeSize.w)) {
            return LayoutOutput::FromOuterSize({outerNodeSize.w, 0.0f});
        }
    }

    // 2. Resolve the explicit grid.
    //
    // Like innerNodeSize, except that an indefinite inner size falls back to a
    // min- or max-size style if the node has one.
    SizeFOpt autoFitContainerSize =
        MaybeSub(MaybeMax(MaybeClamp(Or(Or(outerNodeSize, maxSize), minSize),
                                     minSize, maxSize),
                          paddingBorderSize),
                 contentBoxInset.SumAxes());

    // With a definite size or max size, the number of repetitions is the
    // largest that does not overflow; with only a definite min size, the
    // smallest that meets it; otherwise the list repeats once.
    AutoRepeatStrategy colStrategy =
        (IsSome(outerNodeSize.w) || IsSome(maxSize.w))
            ? AutoRepeatStrategy::MaxRepetitionsThatDoNotOverflow
            : AutoRepeatStrategy::MinRepetitionsThatDoOverflow;
    AutoRepeatStrategy rowStrategy =
        (IsSome(outerNodeSize.h) || IsSome(maxSize.h))
            ? AutoRepeatStrategy::MaxRepetitionsThatDoNotOverflow
            : AutoRepeatStrategy::MinRepetitionsThatDoOverflow;

    ExplicitGridSize colSize = ComputeExplicitGridSizeInAxis(
        style, autoFitContainerSize.w, colStrategy, calc,
        AbsoluteAxis::Horizontal);
    ExplicitGridSize rowSize = ComputeExplicitGridSizeInAxis(
        style, autoFitContainerSize.h, rowStrategy, calc,
        AbsoluteAxis::Vertical);

    NamedLineResolver nameResolver;
    nameResolver
        .Init(style, colSize.autoRepetitionCount, rowSize.autoRepetitionCount);

    uint16_t explicitColCount = colSize.trackCount > nameResolver
                                                         .areaColumnCount
                                    ? colSize.trackCount
                                    : nameResolver.areaColumnCount;
    uint16_t explicitRowCount = rowSize.trackCount > nameResolver.areaRowCount
                                    ? rowSize.trackCount
                                    : nameResolver.areaRowCount;
    explicitColCount =
        explicitColCount > kMaxGridTracks ? kMaxGridTracks : explicitColCount;
    explicitRowCount =
        explicitRowCount > kMaxGridTracks ? kMaxGridTracks : explicitRowCount;
    nameResolver.explicitColumnCount = explicitColCount;
    nameResolver.explicitRowCount = explicitRowCount;

    // The in-flow children, with their names already resolved.
    Vec<PlacementChild> children;
    Vec<ChildPlacementStyles> childPlacements;
    int childCount = tree->ChildCount(node);

    for (int i = 0; i < childCount; i++) {
        NodeId child = tree->GetChildId(node, i);
        const Style& cs = tree->GetStyle(child);
        if (cs.BoxGenMode() == BoxGenerationMode::None ||
            IsOutOfFlow(cs.position)) {
            continue;
        }
        PlacementChild pc;
        pc.index = i;
        pc.node = child;
        pc.horizontal = nameResolver.ResolveColumnNames(cs.gridColumn)
                            .IntoOriginZero(explicitColCount);
        pc.vertical = nameResolver.ResolveRowNames(cs.gridRow)
                          .IntoOriginZero(explicitRowCount);
        VecAppend(children, pc);
        VecAppend(childPlacements, {cs.gridColumn, cs.gridRow});
    }

    // 3. Estimate the implicit track counts, which pre-sizes the occupancy
    //    matrix and is a necessary step in auto-placement.
    TrackCounts estColCounts;
    TrackCounts estRowCounts;
    ComputeGridSizeEstimate(explicitColCount, explicitRowCount,
                            childPlacements.els, len(childPlacements),
                            &estColCounts, &estRowCounts);

    // 4. Grid item placement. Columns stay in logical order; RTL is applied
    // when offsets and item edges are resolved.
    Vec<GridItem> items;
    CellOccupancyMatrix cellOccupancyMatrix;
    cellOccupancyMatrix.Init(estColCounts, estRowCounts);
    PlaceGridItems(&cellOccupancyMatrix, &items, tree, children,
                   style.gridAutoFlow, alignItems, justifyItems);

    // Auto-placement can add tracks, so the counts are read back here.
    TrackCounts finalColCounts = cellOccupancyMatrix
                                     .Counts(AbsoluteAxis::Horizontal);
    TrackCounts finalRowCounts = cellOccupancyMatrix
                                     .Counts(AbsoluteAxis::Vertical);

    // 5. Initialise the tracks and gutters, resolving their sizing functions.
    Vec<GridTrack> columns;
    Vec<GridTrack> rows;
    InitializeGridTracks(&columns, finalColCounts, style,
                         AbsoluteAxis::Horizontal, [&](int columnIndex) {
                             return cellOccupancyMatrix
                                 .ColumnIsOccupied(columnIndex);
                         });
    InitializeGridTracks(&rows, finalRowCounts, style, AbsoluteAxis::Vertical,
                         [&](int rowIndex) {
                             return cellOccupancyMatrix.RowIsOccupied(rowIndex);
                         });

    // 6. Track sizing.
    ResolveItemTrackIndexes(items.els, len(items), finalColCounts,
                            finalRowCounts);
    DetermineIfItemCrossesFlexibleOrIntrinsicTracks(items.els, len(items),
                                                    columns.els, rows.els);

    bool hasBaselineAlignedItem = false;
    for (int i = 0; i < len(items); i++) {
        if (items[i].ParticipatesInBaselineAlignment()) {
            hasBaselineAlignedItem = true;
            break;
        }
    }

    TrackSizingAlgorithm(
        tree, AbstractAxis::Inline, Get(innerMinSize, AbstractAxis::Inline),
        Get(innerMaxSize, AbstractAxis::Inline), justifyContent, alignContent,
        availableGridSpace, innerNodeSize, columns.els, len(columns), rows.els,
        len(rows), items.els, len(items),
        TrackSizeEstimate::MaxTrackSizingFunction, hasBaselineAlignedItem);
    float initialColumnSum = 0.0f;
    for (int i = 0; i < len(columns); i++) {
        initialColumnSum += columns[i].baseSize;
    }
    if (!IsSome(innerNodeSize.w)) {
        innerNodeSize.w = Some(initialColumnSum);
    }

    // Only the container's width was requested. Row sizing measures every
    // item's height, and column sizing is not re-run for a size-only pass.
    // A known width has already returned above.
    if (runMode == RunMode::ComputeSize &&
        inputs.axis == RequestedAxis::Horizontal) {
        float width = F32Max(
            MaybeClamp(initialColumnSum + contentBoxInset.HorizontalAxisSum(),
                       minSize.w, maxSize.w),
            paddingBorderSize.w);
        VecReset(items);
        VecReset(columns);
        VecReset(rows);
        VecReset(children);
        VecReset(childPlacements);
        cellOccupancyMatrix.Free();
        nameResolver.Free();
        return LayoutOutput::FromOuterSize({width, 0.0f});
    }

    for (int i = 0; i < len(items); i++) {
        items[i].hasGridAreaSizeCache = false;
    }

    TrackSizingAlgorithm(
        tree, AbstractAxis::Block, Get(innerMinSize, AbstractAxis::Block),
        Get(innerMaxSize, AbstractAxis::Block), alignContent, justifyContent,
        availableGridSpace, innerNodeSize, rows.els, len(rows), columns.els,
        len(columns), items.els, len(items), TrackSizeEstimate::BaseSize,
        // TODO(taffy): baseline alignment in the block axis.
        false);
    float initialRowSum = 0.0f;
    for (int i = 0; i < len(rows); i++) {
        initialRowSum += rows[i].baseSize;
    }

    // 6. Container size.
    SizeFOpt resolvedStyleSize = Or(knownDimensions, preferredSize);
    SizeF containerBorderBox = {
        F32Max(MaybeClamp(UnwrapOr(Get(resolvedStyleSize, AbstractAxis::Inline),
                                   initialColumnSum + contentBoxInset
                                                          .HorizontalAxisSum()),
                          minSize.w, maxSize.w),
               paddingBorderSize.w),
        F32Max(MaybeClamp(
                   UnwrapOr(Get(resolvedStyleSize, AbstractAxis::Block),
                            initialRowSum + contentBoxInset.VerticalAxisSum()),
                   minSize.h, maxSize.h),
               paddingBorderSize.h)};
    SizeF containerContentBox = {
        F32Max(0.0f, containerBorderBox.w - contentBoxInset
                                                .HorizontalAxisSum()),
        F32Max(0.0f, containerBorderBox.h - contentBoxInset.VerticalAxisSum())};

    if (runMode == RunMode::ComputeSize) {
        VecReset(items);
        VecReset(columns);
        VecReset(rows);
        VecReset(children);
        VecReset(childPlacements);
        cellOccupancyMatrix.Free();
        nameResolver.Free();
        return LayoutOutput::FromOuterSize(containerBorderBox);
    }

    // The container's size is now determined, so a later track-sizing re-run
    // resolves percentages against its content box. This is a no-op when that
    // size was already known.
    innerNodeSize = {Some(containerContentBox.w), Some(containerContentBox.h)};

    // Column sizing is re-run once if the container's width was indefinite and
    // there are percentage columns, or if any item's min-content contribution
    // across an intrinsic track changed.
    bool hasPercentageColumn = false;
    for (int i = 0; i < len(columns); i++) {
        if (columns[i].UsesPercentage()) {
            hasPercentageColumn = true;
            break;
        }
    }
    bool hasPercentageRow = false;
    for (int i = 0; i < len(rows); i++) {
        if (rows[i].UsesPercentage()) {
            hasPercentageRow = true;
            break;
        }
    }
    // The container's own size, not the available space. A shrink-to-fit
    // grid can have a definite available width and an indefinite size.
    bool parentWidthIndefinite = !IsSome(outerNodeSize.w);
    bool rerunColumnSizing = parentWidthIndefinite && hasPercentageColumn;
    bool intrinsicColumnContributionChanged = false;

    if (!rerunColumnSizing) {
        // Every item that crosses an intrinsic column is visited. Stopping
        // at the first change would leave the later items' caches stale.
        for (int i = 0; i < len(items); i++) {
            GridItem* item = &items[i];
            if (!item->crossesIntrinsicColumn) {
                continue;
            }
            SizeFOpt gridAreaSize = ItemGridAreaSize(
                *item, AbstractAxis::Inline, columns.els, rows.els,
                innerNodeSize, TrackSizeEstimate::BaseSize, calc);
            SizeFOpt avail = gridAreaSize;
            Set(&avail, AbstractAxis::Inline, None());
            float newMinContent = ItemMinContentContribution(
                *item, AbstractAxis::Inline, tree, gridAreaSize, avail);
            bool changed =
                !(IsSome(item->minContentContributionCache.w) &&
                  item->minContentContributionCache.w == newMinContent);
            item->gridAreaSizeCache = gridAreaSize;
            item->hasGridAreaSizeCache = true;
            item->minContentContributionCache.w = Some(newMinContent);
            item->maxContentContributionCache.w = None();
            item->minimumContributionCache.w = None();
            if (changed) {
                intrinsicColumnContributionChanged = true;
            }
        }
        rerunColumnSizing = intrinsicColumnContributionChanged;
    } else {
        for (int i = 0; i < len(items); i++) {
            items[i].hasGridAreaSizeCache = false;
            items[i].minContentContributionCache.w = None();
            items[i].maxContentContributionCache.w = None();
            items[i].minimumContributionCache.w = None();
        }
    }

    // Row sizing is re-run once if the container's height was indefinite and
    // there are percentage rows, or if column sizing was re-run and an item's
    // min-content contribution across an intrinsic row changed. It is not
    // nested in the column re-run: a percentage row still resolves when no
    // column needs another pass.
    bool parentHeightIndefinite = !IsSome(outerNodeSize.h);
    bool rerunRowSizing = parentHeightIndefinite && hasPercentageRow;
    bool intrinsicRowContributionChanged = false;
    if (rerunRowSizing) {
        for (int i = 0; i < len(items); i++) {
            items[i].hasGridAreaSizeCache = false;
            items[i].minContentContributionCache.h = None();
            items[i].maxContentContributionCache.h = None();
            items[i].minimumContributionCache.h = None();
        }
    }

    if (rerunColumnSizing) {
        TrackSizingAlgorithm(
            tree, AbstractAxis::Inline, Get(innerMinSize, AbstractAxis::Inline),
            Get(innerMaxSize, AbstractAxis::Inline), justifyContent,
            alignContent, availableGridSpace, innerNodeSize, columns.els,
            len(columns), rows.els, len(rows), items.els, len(items),
            TrackSizeEstimate::BaseSize, hasBaselineAlignedItem);

        // Column widths may have changed, so intrinsic heights are recomputed
        // unless the row pass is already going to run.
        if (!rerunRowSizing) {
            // Every item that crosses an intrinsic row is visited. Stopping
            // at the first change would leave the later items' caches stale.
            for (int i = 0; i < len(items); i++) {
                GridItem* item = &items[i];
                if (!item->crossesIntrinsicRow) {
                    continue;
                }
                SizeFOpt gridAreaSize = ItemGridAreaSize(
                    *item, AbstractAxis::Block, rows.els, columns.els,
                    innerNodeSize, TrackSizeEstimate::BaseSize, calc);
                SizeFOpt avail = gridAreaSize;
                Set(&avail, AbstractAxis::Block, None());
                float newMinContent = ItemMinContentContribution(
                    *item, AbstractAxis::Block, tree, gridAreaSize, avail);
                bool changed =
                    !(IsSome(item->minContentContributionCache.h) &&
                      item->minContentContributionCache.h == newMinContent);
                item->gridAreaSizeCache = gridAreaSize;
                item->hasGridAreaSizeCache = true;
                item->minContentContributionCache.h = Some(newMinContent);
                item->maxContentContributionCache.h = None();
                item->minimumContributionCache.h = None();
                if (changed) {
                    intrinsicRowContributionChanged = true;
                }
            }
            rerunRowSizing = intrinsicRowContributionChanged;
        }
    }

    if (rerunRowSizing) {
        TrackSizingAlgorithm(
            tree, AbstractAxis::Block, Get(innerMinSize, AbstractAxis::Block),
            Get(innerMaxSize, AbstractAxis::Block), alignContent,
            justifyContent, availableGridSpace, innerNodeSize, rows.els,
            len(rows), columns.els, len(columns), items.els, len(items),
            TrackSizeEstimate::BaseSize, false);
    }

    if ((intrinsicColumnContributionChanged && !hasPercentageColumn) ||
        (intrinsicRowContributionChanged && !hasPercentageRow)) {
        float finalColumnSum = 0.0f;
        for (int i = 0; i < len(columns); i++) {
            finalColumnSum += columns[i].baseSize;
        }
        float finalRowSum = 0.0f;
        for (int i = 0; i < len(rows); i++) {
            finalRowSum += rows[i].baseSize;
        }
        if (intrinsicColumnContributionChanged && !hasPercentageColumn) {
            containerBorderBox.w = F32Max(
                MaybeClamp(
                    UnwrapOr(Get(resolvedStyleSize, AbstractAxis::Inline),
                             finalColumnSum + contentBoxInset
                                                  .HorizontalAxisSum()),
                    minSize.w, maxSize.w),
                paddingBorderSize.w);
            containerContentBox.w =
                F32Max(0.0f, containerBorderBox.w - contentBoxInset
                                                        .HorizontalAxisSum());
        }
        if (intrinsicRowContributionChanged && !hasPercentageRow) {
            containerBorderBox.h = F32Max(
                MaybeClamp(
                    UnwrapOr(Get(resolvedStyleSize, AbstractAxis::Block),
                             finalRowSum + contentBoxInset.VerticalAxisSum()),
                    minSize.h, maxSize.h),
                paddingBorderSize.h);
            containerContentBox.h = F32Max(
                0.0f, containerBorderBox.h - contentBoxInset.VerticalAxisSum());
        }
    }

    // 8. Track alignment.
    float inlineSizeWithoutScrollbar =
        F32Max(containerBorderBox.w - paddingBorderSize.w, 0.0f);
    float inlineScrollbarGutterForAlignment =
        F32Min(scrollbarGutter.x, inlineSizeWithoutScrollbar);
    AlignTracks(
        Get(containerContentBox, AbstractAxis::Inline),
        {padding.left +
             (IsRtl(direction) ? inlineScrollbarGutterForAlignment : 0.0f),
         padding.right +
             (IsRtl(direction) ? 0.0f : inlineScrollbarGutterForAlignment)},
        {border.left, border.right}, columns.els, len(columns), justifyContent,
        IsRtl(direction));
    AlignTracks(Get(containerContentBox, AbstractAxis::Block),
                {padding.top, padding.bottom}, {border.top, border.bottom},
                rows.els, len(rows), alignContent, false);

    // 9. Size, align and position the in-flow items. Out-of-flow children are
    // candidates, interleaved in document order with candidates bubbled from
    // in-flow subtrees. The positioning pass lays out the ones this grid
    // claims, resolving their grid areas from the snapshot stored here.
    RectF itemOverflowRect = RectF::Zero();
    Vec<OofCandidate> oofPool;
    OofCandidates oofCandidates;
    StoreGridOofSnapshot(tree, node, colSize.autoRepetitionCount,
                         rowSize.autoRepetitionCount, finalColCounts,
                         finalRowCounts, columns, rows);

    // Back into source order, so items line up with their styles again.
    StableSort(items.els, len(items), [](const GridItem& a, const GridItem& b) {
        return a.sourceOrder < b.sourceOrder;
    });

    for (int index = 0; index < len(items); index++) {
        GridItem& item = items[index];
        // Logical column lines. In RTL the physical left edge is the logical
        // end line and the physical right edge is the logical start line,
        // because offsets were assigned right-to-left.
        float gridLeft = IsRtl(direction)
                             ? columns[(int)item.columnIndexes.end - 1].offset
                             : columns[(int)item.columnIndexes.start + 1]
                                   .offset;
        float gridRight = IsRtl(direction)
                              ? columns[(int)item.columnIndexes.start].offset
                              : columns[(int)item.columnIndexes.end].offset;
        RectF gridArea = {gridLeft, gridRight,
                          rows[(int)item.rowIndexes.start + 1].offset,
                          rows[(int)item.rowIndexes.end].offset};
        AlignedItem placed = AlignAndPositionItem(
            tree, item.node, (uint32_t)index, gridArea, justifyItems,
            alignItems, item.baselineShim, direction, containerBorderBox.w,
            border, isScrollContainer, &oofPool);
        item.yPosition = placed.yPosition;
        item.height = placed.height;
        item.oofStart = placed.oofStart;
        item.oofCount = placed.oofCount;
        itemOverflowRect = itemOverflowRect.Union(placed.overflowContribution);
    }

    int itemCursor = 0;
    uint32_t order = (uint32_t)len(items);
    ContainingBlockClaims claims = ContainingBlockFor(style.position);
    auto lineAsStartEdge = [](const Vec<GridTrack>& tracks, int index) {
        return index + 1 < len(tracks) ? tracks[index + 1].offset
                                       : tracks[index].offset;
    };
    auto lineAsEndEdge = [](const Vec<GridTrack>& tracks, int index) {
        if (index == 0) {
            return len(tracks) > 1 ? tracks[1].offset : tracks[0].offset;
        }
        return tracks[index].offset;
    };
    auto rtlLineAsStartEdge = [](const Vec<GridTrack>& tracks, int index) {
        if (index + 1 < len(tracks)) {
            return tracks[index].offset;
        }
        if (index == 0) {
            return tracks[0].offset;
        }
        return tracks[index - 1].offset;
    };
    auto rtlLineAsEndEdge = [](const Vec<GridTrack>& tracks, int index) {
        if (index == 0) {
            return tracks[0].offset;
        }
        return tracks[index - 1].offset;
    };
    for (int index = 0; index < childCount; index++) {
        if (itemCursor < len(items) &&
            items[itemCursor].sourceOrder == (uint16_t)index) {
            OofCandRange range = {items[itemCursor].oofStart, items[itemCursor]
                                                                  .oofCount};
            OofAppendPoolRange(&oofCandidates, oofPool, range);
            itemCursor += 1;
            continue;
        }

        NodeId child = tree->GetChildId(node, index);
        const Style& cs = tree->GetStyle(child);
        if (cs.BoxGenMode() == BoxGenerationMode::None) {
            tree->SetUnroundedLayout(child, Layout::WithOrder(order));
            tree->PerformChildLayout(
                child, SizeFOptNone(), SizeFOptNone(), SizeAvail::MaxContent(),
                SizingMode::InherentSize, LineBool::False());
            order += 1;
            continue;
        }
        if (!IsOutOfFlow(cs.position)) {
            continue;
        }

        RectF area;
        if (claims.ForPosition(cs.position)) {
            LineOptOzl colLines =
                nameResolver.ResolveColumnNames(cs.gridColumn)
                    .IntoOriginZero(finalColCounts.explicitCount)
                    .ResolveAbsolutelyPositionedGridTracks();
            int colStartIdx = -1;
            int colEndIdx = -1;
            if (colLines.start.IsSome()) {
                TryIntoTrackVecIndex(colLines.start.val, finalColCounts,
                                     &colStartIdx);
            }
            if (colLines.end.IsSome()) {
                TryIntoTrackVecIndex(colLines.end.val, finalColCounts,
                                     &colEndIdx);
            }
            LineOptOzl rowLines =
                nameResolver.ResolveRowNames(cs.gridRow)
                    .IntoOriginZero(finalRowCounts.explicitCount)
                    .ResolveAbsolutelyPositionedGridTracks();
            int rowStartIdx = -1;
            int rowEndIdx = -1;
            if (rowLines.start.IsSome()) {
                TryIntoTrackVecIndex(rowLines.start.val, finalRowCounts,
                                     &rowStartIdx);
            }
            if (rowLines.end.IsSome()) {
                TryIntoTrackVecIndex(rowLines.end.val, finalRowCounts,
                                     &rowEndIdx);
            }
            area.top = rowStartIdx >= 0 ? lineAsStartEdge(rows, rowStartIdx)
                                        : border.top;
            area.bottom =
                rowEndIdx >= 0
                    ? lineAsEndEdge(rows, rowEndIdx)
                    : containerBorderBox.h - border.bottom - scrollbarGutter.y;
            if (IsRtl(direction)) {
                area.left = colEndIdx >= 0
                                ? rtlLineAsEndEdge(columns, colEndIdx)
                                : border.left + scrollbarGutter.x;
                area.right = colStartIdx >= 0
                                 ? rtlLineAsStartEdge(columns, colStartIdx)
                                 : containerBorderBox.w - border.right;
            } else {
                area.left = colStartIdx >= 0
                                ? lineAsStartEdge(columns, colStartIdx)
                                : border.left;
                area.right = colEndIdx >= 0
                                 ? lineAsEndEdge(columns, colEndIdx)
                                 : containerBorderBox.w - border.right -
                                       scrollbarGutter.x;
            }
        } else {
            // Not the containing block: the static position is the content
            // box, as if the child were the sole item of that area.
            area.left = border.left + padding.left +
                        (IsRtl(direction) ? scrollbarGutter.x : 0.0f);
            area.right = containerBorderBox.w - border.right - padding.right -
                         (IsRtl(direction) ? 0.0f : scrollbarGutter.x);
            area.top = border.top + padding.top;
            area.bottom = containerBorderBox.h - border.bottom -
                          padding.bottom - scrollbarGutter.y;
        }

        // none takes the container's items alignment. normal and stretch
        // fall back to start for the static-position rectangle.
        AlignItems justifySelf =
            ResolveSelfRelative(cs.justifySelf.UnwrapOr(justifyItems),
                                cs.direction, direction, true);
        AlignItems alignSelf = ResolveSelfRelative(
            cs.alignSelf.UnwrapOr(alignItems), cs.direction, direction, false);

        OofCandidate candidate;
        candidate.node = child;
        candidate.order = order;
        candidate.position = cs.position;
        candidate.staticX = AxisStaticPosition::FromAlignment(
            justifySelf, area.left, area.right, IsRtl(direction));
        candidate.staticY = AxisStaticPosition::FromAlignment(
            alignSelf, area.top, area.bottom, false);
        oofCandidates.Push(candidate);
        order += 1;
    }

    RectF absolutePositionInset = border;
    if (IsRtl(direction)) {
        absolutePositionInset.left += scrollbarGutter.x;
    } else {
        absolutePositionInset.right += scrollbarGutter.x;
    }
    absolutePositionInset.bottom += scrollbarGutter.y;

    // End padding is part of a scroll container's overflow. Other boxes
    // do not extend their overflow rect by their own padding. Claimed
    // out-of-flow boxes are unioned by the positioning pass.
    auto withEndPadding = [&](RectF overflowRect) {
        if (isScrollContainer) {
            overflowRect
                .right += IsRtl(direction) ? padding.left : padding.right;
            overflowRect.bottom += padding.bottom;
        }
        return overflowRect;
    };

    LayoutOutput out;
    if (len(items) == 0) {
        out = LayoutOutput::FromSizes(containerBorderBox,
                                      withEndPadding(itemOverflowRect));
    } else {
        // The grid container's first baseline. Sort by row start so the items
        // of the first row are contiguous.
        StableSort(items.els, len(items),
                   [](const GridItem& a, const GridItem& b) {
                       return a.row.start < b.row.start;
                   });
        OriginZeroLine firstRow = items[0].row.start;
        int rowEnd = 0;
        while (rowEnd < len(items) && items[rowEnd].row.start == firstRow) {
            rowEnd++;
        }
        const GridItem* chosen = &items[0];
        for (int i = 0; i < rowEnd; i++) {
            if (items[i].ParticipatesInBaselineAlignment()) {
                chosen = &items[i];
                break;
            }
        }
        // Layout containment suppresses the box's baseline. Paint does not.
        Optf gridContainerBaseline = None();
        if (!contain.SuppressesBaseline()) {
            gridContainerBaseline = Some(
                chosen->yPosition + UnwrapOr(chosen->baseline, chosen->height));
        }
        out = LayoutOutput::FromSizesAndBaselines(
            containerBorderBox, withEndPadding(itemOverflowRect),
            BaselinesFromFirst(gridContainerBaseline));
    }

    out.oofCandidates = oofCandidates;
    out.hasOofPositioningArea = true;
    out.oofPositioningArea.size = containerBorderBox - absolutePositionInset
                                                           .SumAxes();
    out.oofPositioningArea
        .offset = {absolutePositionInset.left, absolutePositionInset.top};

    VecReset(items);
    VecReset(columns);
    VecReset(rows);
    VecReset(children);
    VecReset(childPlacements);
    VecReset(oofPool);
    cellOccupancyMatrix.Free();
    nameResolver.Free();
    return out;
}

// The grid area of an out-of-flow child, from the track snapshot taken at
// the end of grid layout. False when this node is not a grid containing
// block (no snapshot). An empty axis has no stored line, so that edge
// falls back to the padding box passed in.
bool GridOofArea(TaffyTree* tree, NodeId grid, NodeId child, RectF paddingBox,
                 RectF* out) {
    NodeData* d = tree->Get(grid);
    if (!d || !d->gridOof.live) {
        return false;
    }
    const Style& gridStyle = tree->GetStyle(grid);
    const Style& childStyle = tree->GetStyle(child);
    const GridOofSnapshot& snap = d->gridOof;
    NamedLineResolver resolver;
    resolver.Init(gridStyle, snap.columns.autoRepetitions,
                  snap.rows.autoRepetitions);
    resolver.explicitColumnCount = snap.columns.explicitCount;
    resolver.explicitRowCount = snap.rows.explicitCount;

    auto spanIndex = [](OptOriginZeroLine line, TrackCounts counts) {
        if (!line.IsSome()) {
            return -1;
        }
        int16_t minLine = -(int16_t)counts.negativeImplicit;
        int16_t maxLine =
            (int16_t)(counts.explicitCount + counts.positiveImplicit);
        if (line.val.v < minLine || line.val.v > maxLine) {
            return -1;
        }
        int trackVec = 0;
        if (!TryIntoTrackVecIndex(line.val, counts, &trackVec)) {
            return -1;
        }
        return trackVec / 2;
    };
    auto resolveAxis = [&](LinePlacement placement,
                           const GridAxisPositions& axis, float padStart,
                           float padEnd, bool rows, bool reversed) {
        TrackCounts counts;
        counts.negativeImplicit = axis.negativeImplicit;
        counts.explicitCount = axis.explicitCount;
        counts.positiveImplicit = axis.positiveImplicit;
        LinePlain plain = rows ? resolver.ResolveRowNames(placement)
                               : resolver.ResolveColumnNames(placement);
        LineOptOzl lines = plain.IntoOriginZero(axis.explicitCount)
                               .ResolveAbsolutelyPositionedGridTracks();
        int startIdx = spanIndex(lines.start, counts);
        int endIdx = spanIndex(lines.end, counts);
        // A line inside a collapsed auto-fit run uses the track on the far
        // side of that run, so the gutters on both sides are not both counted.
        // A start edge follows the shared gutter; an end edge precedes it.
        auto trackEdge = [&](int index, bool isStart) {
            int nRuns = len(axis.collapsed);
            int rangeIndex = 0;
            while (rangeIndex < nRuns && axis.collapsed[rangeIndex]
                                                 .end <= index) {
                rangeIndex++;
            }
            int use = index;
            if (rangeIndex < nRuns &&
                index >= axis.collapsed[rangeIndex].start &&
                index < axis.collapsed[rangeIndex].end) {
                const CollapsedRun& range = axis.collapsed[rangeIndex];
                int adjacent = isStart ? range.end : range.start - 1;
                if (adjacent >= 0 && adjacent < len(axis.spans)) {
                    use = adjacent;
                }
            }
            const GridTrackSpan& track = axis.spans[use];
            return (isStart != reversed) ? track.start : track.end;
        };
        float startPos;
        float endPos;
        if (startIdx >= 0 && startIdx < len(axis.spans)) {
            startPos = trackEdge(startIdx, true);
        } else if (startIdx >= 0 && len(axis.spans) > 0) {
            const GridTrackSpan& last = axis.spans[len(axis.spans) - 1];
            startPos = reversed ? last.start : last.end;
        } else {
            startPos = reversed ? padEnd : padStart;
        }
        if (endIdx > 0 && endIdx - 1 < len(axis.spans)) {
            endPos = trackEdge(endIdx - 1, false);
        } else if (endIdx >= 0 && len(axis.spans) > 0) {
            endPos = reversed ? axis.spans[0].end : axis.spans[0].start;
        } else {
            endPos = reversed ? padStart : padEnd;
        }
        // An area that sits entirely inside a collapsed run is zero-sized
        // and stays on its logical start edge.
        float left;
        float right;
        if (reversed) {
            left = F32Min(startPos, endPos);
            right = startPos;
        } else {
            left = startPos;
            right = F32Max(startPos, endPos);
        }
        return RectF{left, right, 0.0f, 0.0f};
    };

    RectF cols =
        resolveAxis(childStyle.gridColumn, snap.columns, paddingBox.left,
                    paddingBox.right, false, IsRtl(gridStyle.direction));
    RectF row = resolveAxis(childStyle.gridRow, snap.rows, paddingBox.top,
                            paddingBox.bottom, true, false);
    resolver.Free();
    *out = {cols.left, cols.right, row.left, row.right};
    return true;
}

// ─── test seams ──────────────────────────────────────────────────────────
//
// See compute.h: these exist so tests/TaffyTests.cpp can reach the three
// internals taffy's own unit tests cover, and have no other caller.

void GridExplicitSizeForTest(const Style& style, Optf autoFitContainerSize,
                             bool maxRepetitions, AbsoluteAxis axis,
                             CalcResolver calc, uint16_t* outAutoRepetitions,
                             uint16_t* outTrackCount) {
    ExplicitGridSize r = ComputeExplicitGridSizeInAxis(
        style, autoFitContainerSize,
        maxRepetitions ? AutoRepeatStrategy::MaxRepetitionsThatDoNotOverflow
                       : AutoRepeatStrategy::MinRepetitionsThatDoOverflow,
        calc, axis);
    *outAutoRepetitions = r.autoRepetitionCount;
    *outTrackCount = r.trackCount;
}

void GridChildMinMaxSpanForTest(LinePlacement line, uint16_t explicitTrackCount,
                                int16_t* outMinLine, int16_t* outMaxLine,
                                uint16_t* outSpan) {
    MinMaxSpan r = ChildMinLineMaxLineSpan(line, explicitTrackCount);
    *outMinLine = r.minLine.v;
    *outMaxLine = r.maxLine.v;
    *outSpan = r.span;
}

void GridSizeEstimateForTest(uint16_t explicitColCount,
                             uint16_t explicitRowCount,
                             const LinePlacement* columns,
                             const LinePlacement* rows, int n,
                             uint16_t* outColCounts, uint16_t* outRowCounts) {
    Vec<ChildPlacementStyles> children;
    for (int i = 0; i < n; i++) {
        VecAppend(children, {columns[i], rows[i]});
    }
    TrackCounts cols;
    TrackCounts rws;
    ComputeGridSizeEstimate(explicitColCount, explicitRowCount, children.els,
                            len(children), &cols, &rws);
    VecReset(children);
    outColCounts[0] = cols.negativeImplicit;
    outColCounts[1] = cols.explicitCount;
    outColCounts[2] = cols.positiveImplicit;
    outRowCounts[0] = rws.negativeImplicit;
    outRowCounts[1] = rws.explicitCount;
    outRowCounts[2] = rws.positiveImplicit;
}

int GridInitTracksForTest(const Style& style, AbsoluteAxis axis,
                          uint16_t negativeImplicit, uint16_t explicitCount,
                          uint16_t positiveImplicit, GridTrackForTest* out,
                          int cap) {
    Vec<GridTrack> tracks;
    TrackCounts counts =
        TrackCounts::FromRaw(negativeImplicit, explicitCount, positiveImplicit);
    InitializeGridTracks(&tracks, counts, style, axis,
                         [](int) { return false; });
    int n = len(tracks);
    for (int i = 0; i < n && i < cap; i++) {
        out[i].isGutter = tracks[i].kind == GridTrackKind::Gutter;
        out[i].isCollapsed = tracks[i].isCollapsed;
        out[i].min = tracks[i].minTrackSizingFunction.raw;
        out[i].max = tracks[i].maxTrackSizingFunction.raw;
    }
    VecReset(tracks);
    return n;
}

int GridPlaceForTest(TaffyTree* tree, NodeId parent, uint16_t explicitColCount,
                     uint16_t explicitRowCount, GridAutoFlow flow,
                     GridPlacementForTest* out, int cap, uint16_t* outColCounts,
                     uint16_t* outRowCounts) {
    NamedLineResolver nameResolver;
    nameResolver.Init(tree->GetStyle(parent), 0, 0);
    nameResolver.explicitColumnCount = explicitColCount;
    nameResolver.explicitRowCount = explicitRowCount;

    Vec<PlacementChild> children;
    Vec<ChildPlacementStyles> childPlacements;
    int childCount = tree->ChildCount(parent);
    for (int i = 0; i < childCount; i++) {
        NodeId child = tree->GetChildId(parent, i);
        const Style& cs = tree->GetStyle(child);
        if (cs.BoxGenMode() == BoxGenerationMode::None ||
            IsOutOfFlow(cs.position)) {
            continue;
        }
        PlacementChild pc;
        pc.index = i;
        pc.node = child;
        pc.horizontal = nameResolver.ResolveColumnNames(cs.gridColumn)
                            .IntoOriginZero(explicitColCount);
        pc.vertical = nameResolver.ResolveRowNames(cs.gridRow)
                          .IntoOriginZero(explicitRowCount);
        VecAppend(children, pc);
        VecAppend(childPlacements, {cs.gridColumn, cs.gridRow});
    }

    TrackCounts estCols;
    TrackCounts estRows;
    ComputeGridSizeEstimate(explicitColCount, explicitRowCount,
                            childPlacements.els, len(childPlacements), &estCols,
                            &estRows);

    Vec<GridItem> items;
    CellOccupancyMatrix matrix;
    matrix.Init(estCols, estRows);
    PlaceGridItems(&matrix, &items, tree, children, flow,
                   AlignItems{AlignItemsKeyword::Start},
                   AlignItems{AlignItemsKeyword::Start});

    int n = len(items);
    for (int i = 0; i < n && i < cap; i++) {
        out[i].columnStart = items[i].column.start.v;
        out[i].columnEnd = items[i].column.end.v;
        out[i].rowStart = items[i].row.start.v;
        out[i].rowEnd = items[i].row.end.v;
    }
    TrackCounts cols = matrix.Counts(AbsoluteAxis::Horizontal);
    TrackCounts rws = matrix.Counts(AbsoluteAxis::Vertical);
    outColCounts[0] = cols.negativeImplicit;
    outColCounts[1] = cols.explicitCount;
    outColCounts[2] = cols.positiveImplicit;
    outRowCounts[0] = rws.negativeImplicit;
    outRowCounts[1] = rws.explicitCount;
    outRowCounts[2] = rws.positiveImplicit;

    VecReset(items);
    VecReset(children);
    VecReset(childPlacements);
    matrix.Free();
    nameResolver.Free();
    return n;
}

} // namespace taffy
