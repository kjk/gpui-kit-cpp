/* Balanced line breaking for flex-wrap: balance.
 *
 * taffy/src/compute/flexbox.rs `mod balance` at 1bade665. Items are divided
 * into exactly `lineCount` contiguous lines, where `lineCount` is the greedy
 * line count (item sizes floored at zero) raised to `minLineCount` and
 * clamped to the item count. The sum of squared line sizes is minimized.
 * Ties give the most items to the earliest lines. A zero-sized item is glued
 * to the preceding line when a valid division can do so.
 *
 * https://drafts.csswg.org/css-flexbox-2/#balancing
 *
 * `NaiveBalancedLineItemCounts` is the spec's O(k*n^2) program, kept as the
 * test oracle for the divide-and-conquer implementation.
 */

#ifndef GPUI_TAFFY_FLEX_BALANCE_H_
#define GPUI_TAFFY_FLEX_BALANCE_H_

#include "taffy/taffy_math.h"

namespace taffy {

constexpr double kFlexBalanceInfeasible = (double)INFINITY;

struct FlexBalancePrefix {
    double sum = 0.0;
    bool zero = false;
};

struct FlexBalanceSizes {
    Vec<FlexBalancePrefix> sums;
    double gap = 0.0;
    double limit = 0.0;

    int ItemCount() const { return len(sums); }

    bool IsZeroItem(int index) const { return sums[index].zero; }

    double LineSize(int start, int end) const {
        double startSum = start == 0 ? 0.0 : sums[start - 1].sum;
        return sums[end].sum - startSum - gap;
    }

    double LineCost(int start, int end) const {
        double size = LineSize(start, end);
        if (end > start && size > limit) {
            return kFlexBalanceInfeasible;
        }
        return size * size;
    }
};

inline double FlexBalanceToSize(float value) {
    double v = (double)value;
    if (v < 0.0) {
        return 0.0;
    }
    // f32::MAX. F32Max is the two-argument max, not this limit.
    constexpr double kF32Max = 3.4028234663852886e+38;
    double cap = kF32Max;
    if (v > cap) {
        return cap;
    }
    return v;
}

inline bool FlexBalanceFillSizes(FlexBalanceSizes* sizes,
                                 const float* itemSizes, int itemCount,
                                 float lineLimit, float gap) {
    sizes->gap = FlexBalanceToSize(gap);
    sizes->limit = (double)lineLimit;
    for (int i = 0; i < itemCount; i++) {
        double size = FlexBalanceToSize(itemSizes[i]);
        double sum = size + sizes->gap;
        if (i > 0) {
            sum += sizes->sums[i - 1].sum;
        }
        FlexBalancePrefix p;
        p.sum = sum;
        p.zero = size == 0.0;
        if (!VecAppend(sizes->sums, p)) {
            return false;
        }
    }
    return true;
}

// First index in a[0, n) whose value is >= bound. `bound` is compared as
// the integer end index stored in the array.
inline int FlexBalancePartitionGe(const uint32_t* a, int n, uint32_t bound) {
    int lo = 0;
    int hi = n;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] < bound) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

// First index whose value is > bound.
inline int FlexBalancePartitionGt(const uint32_t* a, int n, uint32_t bound) {
    int lo = 0;
    int hi = n;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] <= bound) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

struct FlexBalanceRow {
    const FlexBalanceSizes* sizes;
    const uint32_t* fitEnds;
    const double* prev;
    const uint32_t* nonzeroEnds;
    int maxEnd;
};

inline void FlexBalanceFillRow(const FlexBalanceRow* row, double* cur,
                               uint32_t* opts, int startLo, int startHi,
                               int colLo, int colHi) {
    if (startLo > startHi) {
        return;
    }
    int start = startLo + (startHi - startLo) / 2;
    int sliceN = colHi - colLo;
    int firstCol = colLo + FlexBalancePartitionGe(row->nonzeroEnds + colLo,
                                                  sliceN, (uint32_t)start);
    double minCost = kFlexBalanceInfeasible;
    int minCol = -1;
    for (int col = firstCol; col < colHi; col++) {
        int end = (int)row->nonzeroEnds[col];
        double lineCost = row->sizes->LineCost(start, end);
        if (lineCost == kFlexBalanceInfeasible) {
            break;
        }
        double cost = lineCost + row->prev[end + 1];
        // <= keeps the largest minimizing end.
        if (cost <= minCost) {
            minCost = cost;
            minCol = col;
        }
    }

    int minEnd = minCol < 0 ? -1 : (int)row->nonzeroEnds[minCol];
    int zeroEnd = (int)row->fitEnds[start];
    if (zeroEnd > row->maxEnd) {
        zeroEnd = row->maxEnd;
    }
    if (row->sizes->IsZeroItem(zeroEnd + 1)) {
        double cost =
            row->sizes->LineCost(start, zeroEnd) + row->prev[zeroEnd + 1];
        if (cost < minCost || (cost == minCost && minEnd < zeroEnd)) {
            minCost = cost;
            minEnd = zeroEnd;
        }
    }

    cur[start] = minCost;
    opts[start] = (uint32_t)(minEnd < 0 ? start : minEnd);

    if (start > startLo) {
        int nextHi = minCol < 0 ? colHi : minCol + 1;
        FlexBalanceFillRow(row, cur, opts, startLo, start - 1, colLo, nextHi);
    }
    if (start < startHi) {
        int nextLo = minCol < 0 ? firstCol : minCol;
        FlexBalanceFillRow(row, cur, opts, start + 1, startHi, nextLo, colHi);
    }
}

inline int FlexBalanceGreedyCount(const FlexBalanceSizes* sizes) {
    int itemCount = sizes->ItemCount();
    int lineCount = 1;
    int index = 0;
    while (index < itemCount) {
        int next = index;
        while (next < itemCount &&
               sizes->LineSize(index, next) <= sizes->limit) {
            next++;
        }
        if (next == index) {
            next = index + 1;
        }
        index = next;
        if (index < itemCount) {
            lineCount++;
        }
    }
    return lineCount;
}

inline bool FlexBalanceZeroBoundaryValid(const FlexBalanceSizes* sizes,
                                         const double* minErrors, int lines,
                                         int start, int end) {
    int itemCount = sizes->ItemCount();
    if (!sizes->IsZeroItem(end + 1)) {
        return true;
    }
    int last = itemCount - lines;
    for (int glued = end + 1; glued <= last; glued++) {
        if (sizes->LineSize(start, glued) > sizes->limit) {
            break;
        }
        double rest = minErrors[(lines - 2) * itemCount + glued + 1];
        if (rest != kFlexBalanceInfeasible && rest == rest) {
            return false;
        }
    }
    return true;
}

// Writes one entry per line into `out`. Returns the line count. Every count
// is non-zero and the counts sum to `itemCount`. `itemCount` must be > 0 and
// `out` must hold `itemCount` ints.
inline int BalancedLineItemCounts(const float* itemSizes, int itemCount,
                                  float lineLimit, float gapBetweenItems,
                                  int minLineCount, int* out) {
    FlexBalanceSizes sizes;
    if (!FlexBalanceFillSizes(&sizes, itemSizes, itemCount, lineLimit,
                              gapBetweenItems)) {
        return 0;
    }

    Vec<uint32_t> suffixGreedy;
    for (int i = 0; i < itemCount + 1; i++) {
        VecAppend(suffixGreedy, (uint32_t)0);
    }
    int lineEnd = itemCount;
    for (int start = itemCount - 1; start >= 0; start--) {
        while (lineEnd > start + 1 &&
               sizes.LineSize(start, lineEnd - 1) > sizes.limit) {
            lineEnd--;
        }
        suffixGreedy[start] = 1 + suffixGreedy[lineEnd];
    }

    int minLines = minLineCount;
    if (minLines < 1) {
        minLines = 1;
    }
    if (minLines > itemCount) {
        minLines = itemCount;
    }
    int lineCount = (int)suffixGreedy[0];
    if (lineCount < minLines) {
        lineCount = minLines;
    }

    if (lineCount == itemCount) {
        for (int i = 0; i < itemCount; i++) {
            out[i] = 1;
        }
        return itemCount;
    }

    Vec<uint32_t> fitEnds;
    int fitEnd = 0;
    for (int start = 0; start < itemCount; start++) {
        if (fitEnd < start) {
            fitEnd = start;
        }
        while (fitEnd + 1 < itemCount &&
               sizes.LineSize(start, fitEnd + 1) <= sizes.limit) {
            fitEnd++;
        }
        VecAppend(fitEnds, (uint32_t)fitEnd);
    }

    Vec<uint32_t> nonzeroEnds;
    for (int end = 0; end < itemCount - 1; end++) {
        if (!sizes.IsZeroItem(end + 1)) {
            VecAppend(nonzeroEnds, (uint32_t)end);
        }
    }

    Vec<double> prev;
    for (int start = 0; start < itemCount; start++) {
        VecAppend(prev, sizes.LineCost(start, itemCount - 1));
    }
    Vec<double> cur;
    for (int start = 0; start < itemCount; start++) {
        VecAppend(cur, kFlexBalanceInfeasible);
    }
    Vec<uint32_t> opts;
    int optCount = (lineCount - 1) * itemCount;
    for (int i = 0; i < optCount; i++) {
        VecAppend(opts, (uint32_t)0);
    }

    for (int lines = 2; lines <= lineCount; lines++) {
        int maxEnd = itemCount - lines;
        int firstStart = 0;
        while (firstStart < maxEnd && (int)suffixGreedy[firstStart] > lines) {
            cur[firstStart] = kFlexBalanceInfeasible;
            firstStart++;
        }
        int colHi = FlexBalancePartitionGt(nonzeroEnds.els, len(nonzeroEnds),
                                           (uint32_t)maxEnd);
        FlexBalanceRow row;
        row.sizes = &sizes;
        row.fitEnds = fitEnds.els;
        row.prev = prev.els;
        row.nonzeroEnds = nonzeroEnds.els;
        row.maxEnd = maxEnd;
        FlexBalanceFillRow(&row, cur.els, opts.els + (lines - 2) * itemCount,
                           firstStart, maxEnd, 0, colHi);
        prev = cur;
    }

    int start = 0;
    int written = 0;
    for (int lines = lineCount; lines >= 2; lines--) {
        int end = (int)opts[(lines - 2) * itemCount + start];
        out[written++] = end - start + 1;
        start = end + 1;
    }
    out[written++] = itemCount - start;
    return written;
}

// The naive dynamic program. Same contract as BalancedLineItemCounts.
inline int NaiveBalancedLineItemCounts(const float* itemSizes, int itemCount,
                                       float lineLimit, float gapBetweenItems,
                                       int minLineCount, int* out) {
    FlexBalanceSizes sizes;
    if (!FlexBalanceFillSizes(&sizes, itemSizes, itemCount, lineLimit,
                              gapBetweenItems)) {
        return 0;
    }
    int minLines = minLineCount;
    if (minLines < 1) {
        minLines = 1;
    }
    if (minLines > itemCount) {
        minLines = itemCount;
    }
    int lineCount = FlexBalanceGreedyCount(&sizes);
    if (lineCount < minLines) {
        lineCount = minLines;
    }

    Vec<double> minErrors;
    for (int start = 0; start < itemCount; start++) {
        VecAppend(minErrors, sizes.LineCost(start, itemCount - 1));
    }
    for (int lines = 2; lines <= lineCount; lines++) {
        int row = (lines - 1) * itemCount;
        for (int start = 0; start < itemCount; start++) {
            double minError = kFlexBalanceInfeasible;
            int end = start;
            while (end + lines <= itemCount) {
                double lineCost = sizes.LineCost(start, end);
                if (lineCost == kFlexBalanceInfeasible) {
                    break;
                }
                if (FlexBalanceZeroBoundaryValid(&sizes, minErrors.els, lines,
                                                 start, end)) {
                    double error =
                        lineCost + minErrors[row - itemCount + end + 1];
                    if (error < minError) {
                        minError = error;
                    }
                }
                end++;
            }
            VecAppend(minErrors, minError);
        }
    }

    int start = 0;
    int written = 0;
    for (int linesAfter = lineCount - 1; linesAfter >= 0; linesAfter--) {
        double target = minErrors[linesAfter * itemCount + start];
        int end = itemCount - 1 - linesAfter;
        for (;;) {
            bool fits = sizes.LineCost(start, end) != kFlexBalanceInfeasible;
            if (fits && linesAfter != 0) {
                fits = FlexBalanceZeroBoundaryValid(&sizes, minErrors.els,
                                                    linesAfter + 1, start, end);
            }
            if (fits) {
                double remaining = 0.0;
                if (linesAfter != 0) {
                    remaining =
                        minErrors[(linesAfter - 1) * itemCount + end + 1];
                }
                if (sizes.LineCost(start, end) + remaining == target) {
                    break;
                }
            }
            end--;
        }
        out[written++] = end - start + 1;
        start = end + 1;
    }
    return written;
}

} // namespace taffy

#endif // GPUI_TAFFY_FLEX_BALANCE_H_
