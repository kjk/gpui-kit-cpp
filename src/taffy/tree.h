/* The data a layout run passes down and hands back, and the per-node cache —
 * taffy/src/tree/{node,layout,cache}.rs.
 *
 * Deviations from the Rust:
 *   - `NodeId` wraps a slotmap key there. The tree here keeps its own
 *     generational slots, so a NodeId is index + generation packed into the
 *     same u64 and converts to and from one the way Rust's does.
 *   - `LayoutOutput::scrollable_overflow_rect` and
 *     `Layout::scrollable_overflow_rect` are unconditional; the
 *     `content_size` feature is on in the build gpui-kit pins.
 */

#ifndef GPUI_TAFFY_TREE_H_
#define GPUI_TAFFY_TREE_H_

#include "taffy/style.h"

namespace taffy {

// ─── NodeId ──────────────────────────────────────────────────────────────

// The id of a single node in a tree of nodes. Rust's is a wrapper over a u64
// and converts to and from one; so is this.
struct NodeId {
    uint64_t raw = 0;

    static constexpr NodeId New(uint64_t v) { return NodeId{v}; }
    constexpr uint64_t AsU64() const { return raw; }
};

constexpr bool operator==(NodeId a, NodeId b) {
    return a.raw == b.raw;
}

constexpr bool operator!=(NodeId a, NodeId b) {
    return a.raw != b.raw;
}

// ─── run modes ───────────────────────────────────────────────────────────

// Whether we are performing a full layout or merely sizing the node.
enum class RunMode : uint8_t {
    // A full layout for this node and all its children.
    PerformLayout,
    // Run only far enough to determine an accurate container size for this
    // node; steps that do not affect that can be skipped.
    ComputeSize,
    // Set a null layout — the node is hidden (display: none).
    PerformHiddenLayout
};

// Whether styles are taken into account when computing size.
enum class SizingMode : uint8_t {
    // Content contributions only.
    ContentSize,
    // Inherent size styles as well as content contributions.
    InherentSize
};

// An axis a layout algorithm can be asked to compute a size for.
enum class RequestedAxis : uint8_t {
    Horizontal,
    Vertical,
    Both
};

constexpr RequestedAxis ToRequestedAxis(AbsoluteAxis a) {
    return a == AbsoluteAxis::Horizontal ? RequestedAxis::Horizontal
                                         : RequestedAxis::Vertical;
}

// ─── margin collapsing ───────────────────────────────────────────────────

// A set of margins available for collapsing, for block layout.
struct CollapsibleMarginSet {
    // The largest positive margin.
    float positive = 0.0f;
    // The smallest negative margin (largest absolute value).
    float negative = 0.0f;

    static constexpr CollapsibleMarginSet Zero() { return {}; }

    static CollapsibleMarginSet FromMargin(float margin) {
        if (margin >= 0.0f) {
            return {margin, 0.0f};
        }
        return {0.0f, margin};
    }
    CollapsibleMarginSet CollapseWithMargin(float margin) const {
        CollapsibleMarginSet out = *this;
        if (margin >= 0.0f) {
            out.positive = F32Max(out.positive, margin);
        } else {
            out.negative = F32Min(out.negative, margin);
        }
        return out;
    }
    CollapsibleMarginSet CollapseWithSet(CollapsibleMarginSet other) const {
        return {F32Max(positive, other.positive),
                F32Min(negative, other.negative)};
    }
    // The resultant margin, once everything collapsible has collapsed in.
    float Resolve() const { return positive + negative; }
};

// ─── LayoutInput / LayoutOutput ──────────────────────────────────────────

// The constraints and hints a parent passes down when laying out a node.
struct LayoutInput {
    // Whether we only need the node's size or a full layout.
    RunMode runMode = RunMode::PerformLayout;
    // Whether the node's own size styles count or are ignored.
    SizingMode sizingMode = SizingMode::InherentSize;
    // Which axis the caller needs the size of.
    RequestedAxis axis = RequestedAxis::Both;

    // Dimensions to take as fixed. `knownDimensions.w = Some(W)` asks
    // "what would the height of this node be, assuming the width is W"; both
    // set means "your size is exactly WxH, lay out your children".
    SizeFOpt knownDimensions = SizeFOptNone();
    // Whether a known dimension is a definite size for the node's own
    // content: percentage resolution and flex line breaking. False when a
    // parent imposed a size derived from this node's content. Ignored, and
    // treated as true, on an axis whose known dimension is None.
    bool knownDimensionsAreDefiniteW = true;
    bool knownDimensionsAreDefiniteH = true;
    // The parent's size, for percentage resolution.
    SizeFOpt parentSize = SizeFOptNone();
    // A soft constraint, used for wrapping.
    SizeAvail availableSpace = SizeAvail::MaxContent();
    // Block layout only, for margin collapsing. LineBool::False() otherwise.
    LineBool verticalMarginsAreCollapsible;

    // The input that requests hidden layout; every other field is ignored.
    static LayoutInput Hidden() {
        LayoutInput in;
        in.runMode = RunMode::PerformHiddenLayout;
        return in;
    }
};

// First and last baselines on the horizontal axis, measured from the top
// edge of the border box. `last` is reserved for later commits; this one
// only fills `first`.
struct Baselines {
    Optf first = None();
    Optf last = None();
};

inline Baselines BaselinesNone() {
    return {};
}

inline Baselines BaselinesFromFirst(Optf first) {
    Baselines b;
    b.first = first;
    return b;
}

// One axis of an out-of-flow box's static position. Keywords are physical:
// the layout algorithm that emits the candidate resolves flex, RTL and
// self-start before recording them. A degenerate area (start == end) is an
// anchor point, which is what block layout emits.
enum class AxisStaticEdge : uint8_t {
    Start,
    Center,
    End
};

struct AxisStaticAlign {
    AxisStaticEdge keyword = AxisStaticEdge::Start;
    AlignmentSafety safety = AlignmentSafety::Default;
    AxisStaticEdge fallback = AxisStaticEdge::Start;

    static AxisStaticAlign FromKeyword(AxisStaticEdge keyword) {
        AxisStaticAlign a;
        a.keyword = keyword;
        a.safety = AlignmentSafety::Default;
        a.fallback = keyword;
        return a;
    }
};

struct AxisStaticPosition {
    float start = 0.0f;
    float end = 0.0f;
    AxisStaticAlign align;

    static AxisStaticPosition FromEdge(float anchor, AxisStaticEdge edge) {
        AxisStaticPosition p;
        p.start = anchor;
        p.end = anchor;
        p.align = AxisStaticAlign::FromKeyword(edge);
        return p;
    }

    // Align the box within [areaStart, areaEnd]. `alignment` is already
    // resolved: auto replaced, and self-start/self-end mapped to the
    // container direction. Normal and stretch fall back to start.
    // `axisIsRtl` flips start and end.
    static AxisStaticPosition FromAlignment(AlignItems alignment,
                                            float areaStart, float areaEnd,
                                            bool axisIsRtl) {
        auto edgeFor = [](AlignItemsKeyword keyword, bool rtl) {
            if (keyword == AlignItemsKeyword::Center) {
                return AxisStaticEdge::Center;
            }
            bool endish = keyword == AlignItemsKeyword::End ||
                          keyword == AlignItemsKeyword::FlexEnd;
            bool startPosition = (!endish) != rtl;
            return startPosition ? AxisStaticEdge::Start : AxisStaticEdge::End;
        };
        AlignItems resolved = alignment;
        AlignItemsKeyword fallbackKw = resolved.safety == AlignmentSafety::Safe
                                           ? AlignItemsKeyword::Start
                                           : resolved.keyword;
        AxisStaticPosition p;
        p.start = areaStart;
        p.end = areaEnd;
        p.align.keyword = edgeFor(resolved.keyword, axisIsRtl);
        p.align.safety = resolved.safety;
        p.align.fallback = edgeFor(fallbackKw, axisIsRtl);
        return p;
    }
};

// An absolute or fixed box whose containing block has not laid it out yet.
// Areas are relative to the border box of the node that currently holds the
// candidate, translated by each ancestor's location as it bubbles.
struct OofCandidate {
    NodeId node;
    uint32_t order = 0;
    Position position = Position::Absolute;
    AxisStaticPosition staticX;
    AxisStaticPosition staticY;
};

// Empty lists allocate nothing. The vec is owned here, and `LayoutOutput` is
// copied into the cache by value, so a hit replays the same candidates
// without walking the subtree again.
struct OofCandidates {
    Vec<OofCandidate> inner;

    bool IsEmpty() const { return len(inner) == 0; }
    void Push(OofCandidate candidate) { VecAppend(inner, candidate); }
    void Append(OofCandidates* other) {
        if (!other || other->IsEmpty()) {
            return;
        }
        for (int i = 0; i < len(other->inner); i++) {
            VecAppend(inner, other->inner[i]);
        }
        VecReset(other->inner);
    }
    void Translate(PointF offset) {
        for (int i = 0; i < len(inner); i++) {
            inner[i].staticX.start += offset.x;
            inner[i].staticX.end += offset.x;
            inner[i].staticY.start += offset.y;
            inner[i].staticY.end += offset.y;
        }
    }
    void Clear() { VecReset(inner); }
};

// A slice of `OofCandidates` stored beside an item. The item itself stays
// trivially copyable so it can live in a `Vec`.
struct OofCandRange {
    int start = 0;
    int count = 0;
};

inline void OofAppendRange(Vec<OofCandidate>* pool, OofCandRange* range,
                           const OofCandidates& src) {
    range->start = len(*pool);
    range->count = len(src.inner);
    for (int i = 0; i < range->count; i++) {
        VecAppend(*pool, src.inner[i]);
    }
}

inline void OofTranslateRange(Vec<OofCandidate>* pool, OofCandRange range,
                              PointF offset) {
    for (int i = 0; i < range.count; i++) {
        OofCandidate& c = (*pool)[range.start + i];
        c.staticX.start += offset.x;
        c.staticX.end += offset.x;
        c.staticY.start += offset.y;
        c.staticY.end += offset.y;
    }
}

inline void OofAppendPoolRange(OofCandidates* dst,
                               const Vec<OofCandidate>& pool,
                               OofCandRange range) {
    for (int i = 0; i < range.count; i++) {
        dst->Push(pool[range.start + i]);
    }
}

// Border box minus borders and scrollbar gutters. Insets of out-of-flow
// boxes resolve against this. None on a leaf or a size-only pass.
struct OofPositioningArea {
    SizeF size;
    PointF offset;
};

// The result of laying out a single node, handed back up to the parent.
//
// A baseline is the line text sits on. A node likely has one if it is a text
// node or contains children that may be. A node with no baseline (or one that
// is not sure how to compute it) reports BaselinesNone().
struct LayoutOutput {
    // Vec's default constructor is explicit, so this cannot be an aggregate:
    // `return {}` and `CacheEntry`'s `content{}` would not compile.
    LayoutOutput() = default;

    SizeF size;
    // The scrollable overflow rectangle of the node's content. Same
    // coordinate conventions as Layout::scrollableOverflowRect.
    RectF scrollableOverflowRect;
    // The first and last baselines on the horizontal axis, if any.
    Baselines baselines;
    // Margins that can be collapsed with, for block layout. Zero elsewhere.
    CollapsibleMarginSet topMargin;
    CollapsibleMarginSet bottomMargin;
    // Whether margins can collapse *through* this node. Block layout only.
    bool marginsCanCollapseThrough = false;
    // Absolute and fixed descendants this node did not claim. PerformLayout
    // only. The positioning pass replaces this with the unclaimed remainder
    // before the result is cached.
    OofCandidates oofCandidates;
    // Set for a container PerformLayout. The positioning pass is a no-op
    // when this is false (leaves and size-only runs).
    bool hasOofPositioningArea = false;
    OofPositioningArea oofPositioningArea;

    // An all-zero output, for hidden nodes. Rust's `HIDDEN` and `DEFAULT` are
    // the same value.
    static LayoutOutput Hidden() { return {}; }

    static LayoutOutput FromSizesAndBaselines(SizeF size,
                                              RectF scrollableOverflowRect,
                                              Baselines baselines) {
        LayoutOutput out;
        out.size = size;
        out.scrollableOverflowRect = scrollableOverflowRect;
        out.baselines = baselines;
        return out;
    }
    static LayoutOutput FromSizes(SizeF size, RectF scrollableOverflowRect) {
        return FromSizesAndBaselines(size, scrollableOverflowRect,
                                     BaselinesNone());
    }
    static LayoutOutput FromOuterSize(SizeF size) {
        return FromSizes(size, RectF::Zero());
    }
};

// ─── Layout ──────────────────────────────────────────────────────────────

// The final result of a layout algorithm for a single node.
struct Layout {
    // The relative ordering of the node. A higher order paints on top; this
    // is a topological sort of the tree.
    uint32_t order = 0;
    // The top-left corner of the node.
    PointF location;
    SizeF size;
    // Scrollable overflow of the node, measured from the scroll origin: the
    // padding-box corner at the block-start/inline-start edge (top-left in
    // LTR, top-right in RTL). `left`/`top` are <= 0 (unreachable start
    // overflow). `right`/`bottom` are >= 0 (the reachable extent a scroll
    // width/height is computed from). Transforms are not included.
    RectF scrollableOverflowRect;
    // Zero in each dimension that has no scrollbar.
    SizeF scrollbarSize;
    RectF border;
    RectF padding;
    RectF margin;

    static Layout New() { return {}; }
    static Layout WithOrder(uint32_t order) {
        Layout l;
        l.order = order;
        return l;
    }

    float ContentBoxWidth() const {
        return size.w - padding.left - padding.right - border.left -
               border.right;
    }
    float ContentBoxHeight() const {
        return size.h - padding.top - padding.bottom - border.top -
               border.bottom;
    }
    SizeF ContentBoxSize() const {
        return {ContentBoxWidth(), ContentBoxHeight()};
    }
    // The content box origin, relative to the parent's border box.
    float ContentBoxX() const {
        return location.x + border.left + padding.left;
    }
    float ContentBoxY() const { return location.y + border.top + padding.top; }

    // Reachable content extent minus the padding box, floored at zero.
    float ScrollWidth() const {
        return F32Max(0.0f, scrollableOverflowRect.right +
                                F32Min(scrollbarSize.w, size.w) - size.w +
                                border.left + border.right);
    }
    float ScrollHeight() const {
        return F32Max(0.0f, scrollableOverflowRect.bottom +
                                F32Min(scrollbarSize.h, size.h) - size.h +
                                border.top + border.bottom);
    }
};

// ─── Cache ───────────────────────────────────────────────────────────────

// A node's size is queried by its parent several times over one layout run.
// Measure results share a small clock: a hit marks the slot, and a full
// cache replaces the first slot whose mark was already cleared.
constexpr int kCacheSize = 9;

// A space-optimised cache key. Two f32s packed into a u64 each; the f32s are
// always non-negative, so the two sign bits carry the requested axis.
struct CacheKey {
    uint64_t kdAvailableSpace = 0;
    uint64_t parentSize = 0;
    // Normalized so an axis with no known dimension is always true.
    bool knownDimensionsAreDefiniteW = true;
    bool knownDimensionsAreDefiniteH = true;

    static CacheKey From(const LayoutInput& input);
    // The parent size with the axis bits and the y-axis value masked out.
    uint64_t XAxisParentSize() const;
    // A single-axis measure may leave the other axis garbage, so it only
    // answers a query for that same axis. A both-axis entry answers either.
    bool SizeIsValidFor(CacheKey other) const;
};

constexpr bool operator==(CacheKey a, CacheKey b) {
    return a.kdAvailableSpace == b.kdAvailableSpace &&
           a.parentSize == b.parentSize &&
           a.knownDimensionsAreDefiniteW == b.knownDimensionsAreDefiniteW &&
           a.knownDimensionsAreDefiniteH == b.knownDimensionsAreDefiniteH;
}

// Rust's entry is an `Option<CacheEntry>`; the discriminant lives in
// `Cache::presentMask` here rather than as a bool per entry, so that
// emptying a cache is one store instead of a ~360-byte memset. The tree is
// rebuilt from nothing every frame, so every node's cache is reset every
// frame, and that memset was most of `InsertNode`. Dropping the bool also
// takes the padded entry from 32 bytes to 24, which matters again on the
// lookup side: a story's caches are far larger than L2 and every get and
// store is a miss.
template <typename T>
struct CacheEntry {
    CacheKey key;
    T content{};
};

// Caches the result of sizing a flexbox or grid item.
struct Cache {
    CacheEntry<LayoutOutput> finalLayoutEntry;
    CacheEntry<SizeF> measureEntries[kCacheSize];
    // Bit 0 is finalLayoutEntry; bits 1..kCacheSize are measureEntries.
    // An entry's content is only meaningful while its bit is set.
    uint16_t presentMask = 0;
    // Bit i is set when measure slot i has been used since the clock last
    // passed it. The clock clears the bit and skips a set slot.
    uint16_t recentlyUsedEntries = 0;
    uint8_t nextMeasureEntry = 0;
    // Tracks whether every entry is empty, so a clear can be skipped.
    bool isEmpty = true;
    // Sizing mode and vertical margin collapsibility are not in `CacheKey`.
    // Every entry was computed with this one mode. A get for another mode
    // misses, and a store of another mode drops the entries. Bit 0 is
    // inherent size, bit 1 the start margin, bit 2 the end margin.
    uint8_t mode = 0;

    static uint8_t CacheModeOf(const LayoutInput& input) {
        uint8_t bits = 0;
        if (input.sizingMode == SizingMode::InherentSize) {
            bits = (uint8_t)(bits | 1u);
        }
        if (input.verticalMarginsAreCollapsible.start) {
            bits = (uint8_t)(bits | 2u);
        }
        if (input.verticalMarginsAreCollapsible.end) {
            bits = (uint8_t)(bits | 4u);
        }
        return bits;
    }
    bool ModeMatches(const LayoutInput& input) const {
        return mode == CacheModeOf(input);
    }

    static constexpr uint16_t kFinalBit = 1;
    static constexpr uint16_t MeasureBit(int i) {
        return (uint16_t)(1u << (i + 1));
    }

    bool Get(const LayoutInput& input, LayoutOutput* out);
    void Store(const LayoutInput& input, const LayoutOutput& output);
    // The same two with the key already in hand. Every real lookup is a get
    // followed by a store with the same input, and building the key is a
    // dozen bit operations over four floats — enough, at one per node per
    // pass, to be worth doing once. A measure hit marks the slot used.
    bool GetWithKey(CacheKey key, RunMode runMode, LayoutOutput* out);
    void StoreWithKey(CacheKey key, const LayoutInput& input,
                      const LayoutOutput& output);
    // True if it cleared something, false if it was already empty. Rust
    // returns a `ClearState` enum for the same answer.
    bool Clear();
    bool IsEmpty() const;
};

} // namespace taffy

#endif // GPUI_TAFFY_TREE_H_
