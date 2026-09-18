/* The data a layout run passes down and hands back, and the per-node cache —
 * taffy/src/tree/{node,layout,cache}.rs.
 *
 * Deviations from the Rust:
 *   - `NodeId` wraps a slotmap key there. The tree here keeps its own
 *     generational slots, so a NodeId is index + generation packed into the
 *     same u64 and converts to and from one the way Rust's does.
 *   - `LayoutOutput::content_size` and `Layout::content_size` are
 *     unconditional; the `content_size` feature is on in the build
 *     gpui-kit pins.
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

// The result of laying out a single node, handed back up to the parent.
//
// A baseline is the line text sits on. A node likely has one if it is a text
// node or contains children that may be. A node with no baseline (or one that
// is not sure how to compute it) reports PointFOptNone().
struct LayoutOutput {
    SizeF size;
    // The size of the content within the node.
    SizeF contentSize;
    // The first baseline in each dimension, if any.
    PointFOpt firstBaselines = PointFOptNone();
    // Margins that can be collapsed with, for block layout. Zero elsewhere.
    CollapsibleMarginSet topMargin;
    CollapsibleMarginSet bottomMargin;
    // Whether margins can collapse *through* this node. Block layout only.
    bool marginsCanCollapseThrough = false;

    // An all-zero output, for hidden nodes. Rust's `HIDDEN` and `DEFAULT` are
    // the same value.
    static LayoutOutput Hidden() { return {}; }

    static LayoutOutput FromSizesAndBaselines(SizeF size, SizeF contentSize,
                                              PointFOpt firstBaselines) {
        LayoutOutput out;
        out.size = size;
        out.contentSize = contentSize;
        out.firstBaselines = firstBaselines;
        return out;
    }
    static LayoutOutput FromSizes(SizeF size, SizeF contentSize) {
        return FromSizesAndBaselines(size, contentSize, PointFOptNone());
    }
    static LayoutOutput FromOuterSize(SizeF size) {
        return FromSizes(size, SizeF::Zero());
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
    // The size of the content inside the node. Larger than `size` when the
    // content overflows, which is what a scroll width/height is computed from.
    SizeF contentSize;
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

    // Content width minus width, floored at zero.
    float ScrollWidth() const {
        return F32Max(0.0f, contentSize.w + F32Min(scrollbarSize.w, size.w) -
                                size.w + border.left + border.right);
    }
    float ScrollHeight() const {
        return F32Max(0.0f, contentSize.h + F32Min(scrollbarSize.h, size.h) -
                                size.h + border.top + border.bottom);
    }
};

// ─── Cache ───────────────────────────────────────────────────────────────

// A node's size is queried by its parent several times over one layout run
// and later results must not clobber earlier ones, so there is a slot per
// shape of query. See `ComputeCacheSlot` for what each slot holds.
constexpr int kCacheSize = 9;

// A space-optimised cache key. Two f32s packed into a u64 each; the f32s are
// always non-negative, so the two sign bits carry the requested axis.
struct CacheKey {
    uint64_t kdAvailableSpace = 0;
    uint64_t parentSize = 0;

    static CacheKey From(const LayoutInput& input);
    // The parent size with the axis bits and the y-axis value masked out.
    uint64_t XAxisParentSize() const;
};

constexpr bool operator==(CacheKey a, CacheKey b) {
    return a.kdAvailableSpace == b.kdAvailableSpace &&
           a.parentSize == b.parentSize;
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
    // Tracks whether every entry is empty, so a clear can be skipped.
    bool isEmpty = true;

    static constexpr uint16_t kFinalBit = 1;
    static constexpr uint16_t MeasureBit(int i) {
        return (uint16_t)(1u << (i + 1));
    }

    bool Get(const LayoutInput& input, LayoutOutput* out) const;
    void Store(const LayoutInput& input, const LayoutOutput& output);
    // The same two with the key already in hand. Every real lookup is a get
    // followed by a store with the same input, and building the key is a
    // dozen bit operations over four floats — enough, at one per node per
    // pass, to be worth doing once.
    bool GetWithKey(CacheKey key, RunMode runMode, LayoutOutput* out) const;
    void StoreWithKey(CacheKey key, const LayoutInput& input,
                      const LayoutOutput& output);
    // True if it cleared something, false if it was already empty. Rust
    // returns a `ClearState` enum for the same answer.
    bool Clear();
    bool IsEmpty() const;
};

} // namespace taffy

#endif // GPUI_TAFFY_TREE_H_
