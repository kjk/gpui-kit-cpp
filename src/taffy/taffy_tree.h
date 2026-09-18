/* TaffyTree — the tree that owns the nodes, and the entry point to the
 * high-level API. taffy/src/tree/taffy_tree.rs plus the traits in
 * taffy/src/tree/traits.rs.
 *
 * Deviations from the Rust:
 *   - Rust's `LayoutPartialTree` / `CacheTree` / `LayoutFlexboxContainer` /
 *     `LayoutGridContainer` / `LayoutBlockContainer` / `RoundTree` /
 *     `PrintTree` traits exist so a caller can bring its own tree. There is
 *     one tree here, so the compute pass takes a `TaffyTree*` and the traits
 *     collapse into its methods. The method names are the trait method names.
 *   - `TaffyView` exists in Rust only to keep the measure closure out of the
 *     tree's borrow. The measure function is a field on the tree here, set
 *     for the duration of a `ComputeLayoutWithMeasure` call.
 *   - `NodeContext` is a type parameter there; it is a `void*` here, handed
 *     back to the measure function untouched.
 *   - Rust returns `TaffyResult<T>`. The three calls that can be given an
 *     out-of-range child index return `bool`; everything else takes a valid
 *     node by contract, the way Rust's `.expect()`-ing callers already do.
 *   - `detailed_layout_info` is not stored. It exists for consumers that want
 *     the computed grid track sizes back out; nothing in this tree reads it.
 */

#ifndef GPUI_TAFFY_TAFFY_TREE_H_
#define GPUI_TAFFY_TAFFY_TREE_H_

#include "taffy/tree.h"

namespace taffy {

struct BlockContext;

// Computes the intrinsic size of a leaf node. Rust passes a closure that
// captures the tree; the context and userData here are what it would have
// captured.
using MeasureFn = SizeF (*)(SizeFOpt knownDimensions, SizeAvail availableSpace,
                            NodeId node, void* nodeContext, const Style* style,
                            void* userData);

// The layout data for one node.
struct NodeData {
    Style style;

    // The always-unrounded result. Kept apart from the rounded layout so that
    // rounding never compounds on an already-rounded value.
    Layout unroundedLayout;
    // The final result — rounded or not, depending on `useRounding`.
    Layout finalLayout;

    bool hasContext = false;
    void* context = nullptr;

    Cache cache;

    // Node slots are generational: an id carries the generation it was minted
    // with, so a stale id does not resolve to whatever took the slot.
    uint32_t generation = 0;
    bool alive = false;

    Vec<NodeId> children;
    NodeId parent;
    bool hasParent = false;
};

// An entire tree of UI nodes.
struct TaffyTree {
    // One entry per slot. A slot is reused after its node is removed, with a
    // bumped generation. The NodeData is separately allocated so its address
    // is stable while layout holds references into it.
    Vec<NodeData*> slots;
    Vec<int32_t> freeSlots;
    int32_t liveCount = 0;
    // Times InsertNode had to `new NodeData` since the caller last zeroed
    // this. Recycled slots do not increment it.
    int32_t allocs = 0;

    // Whether to round layout values. On by default, as in Rust.
    bool useRounding = true;

    // Set for the duration of a ComputeLayoutWithMeasure call.
    MeasureFn measureFn = nullptr;
    void* measureUserData = nullptr;

    // Backs the grid track lists in the styles this tree was handed, if the
    // caller wants the tree to own them. Null when the caller keeps its own.
    Arena* styleArena = nullptr;

    // How a calc() handle in a style resolves. Rust's
    // `LayoutPartialTree::resolve_calc_value`, which defaults to 0.
    CalcResolver calc;

    // ── lifecycle ────────────────────────────────────────────────────────

    void Init(int capacity = 16);
    void Free();

    void EnableRounding() { useRounding = true; }
    void DisableRounding() { useRounding = false; }

    // ── building ─────────────────────────────────────────────────────────

    NodeId NewLeaf(const Style& style);
    NodeId NewLeafWithContext(const Style& style, void* context);
    NodeId NewWithChildren(const Style& style, const NodeId* children, int n);

    // Drops every node in the tree.
    void Clear();
    // Detaches `node` from its parent and children and drops it. Descendants
    // stay alive so a caller that walks them (the layout cache) can give
    // their context back before removing each one.
    void Remove(NodeId node);

    // Live nodes that cannot be reached from `root`. `fn` is called with each
    // such id, parents before descendants are not guaranteed; the id is still
    // alive when `fn` runs.
    void EachUnreachable(NodeId root, void (*fn)(NodeId, void*), void* user);

    void SetNodeContext(NodeId node, void* context, bool hasContext);
    void* GetNodeContext(NodeId node) const;

    void AddChild(NodeId parent, NodeId child);
    // False if childIndex is past the end.
    bool InsertChildAtIndex(NodeId parent, int childIndex, NodeId child);
    void SetChildren(NodeId parent, const NodeId* children, int n);
    // The child is detached, not dropped.
    NodeId RemoveChild(NodeId parent, NodeId child);
    // Returns the removed child, or a zero NodeId if the index is past the end.
    NodeId RemoveChildAtIndex(NodeId parent, int childIndex);
    void RemoveChildrenRange(NodeId parent, int start, int end);
    // Returns the child that was replaced, or a zero NodeId if out of range.
    NodeId ReplaceChildAtIndex(NodeId parent, int childIndex, NodeId newChild);

    // ── reading ──────────────────────────────────────────────────────────

    // Rust panics on an out-of-range index; this returns a zero NodeId.
    NodeId ChildAtIndex(NodeId parent, int childIndex) const;
    int TotalNodeCount() const { return liveCount; }
    // Slots that have ever been allocated, including dead ones on the free
    // list. InsertNode `new`s a NodeData only when this grows.
    int SlotCount() const { return len(slots); }
    // hasParent is false for a root.
    NodeId Parent(NodeId child, bool* hasParent) const;

    void SetStyle(NodeId node, const Style& style);
    const Style& GetStyle(NodeId node) const;

    // This node's layout, relative to its parent.
    const Layout& GetLayout(NodeId node) const;
    const Layout& UnroundedLayout(NodeId node) const;

    // Marks this node and its ancestors as needing relayout.
    void MarkDirty(NodeId node);
    bool Dirty(NodeId node) const;

    // ── running layout ───────────────────────────────────────────────────

    void ComputeLayoutWithMeasure(NodeId node, SizeAvail availableSpace,
                                  MeasureFn measure, void* userData);
    void ComputeLayout(NodeId node, SizeAvail availableSpace);

    // Prints a debug representation of the tree's layout. Rust's
    // `TaffyTree::print_tree`.
    void PrintTree(NodeId root);

    // ── the trait surface the compute pass calls ─────────────────────────

    int ChildCount(NodeId parent) const;
    NodeId GetChildId(NodeId parent, int index) const;

    void SetUnroundedLayout(NodeId node, const Layout& layout);
    Layout GetUnroundedLayout(NodeId node) const;
    void SetFinalLayout(NodeId node, const Layout& layout);
    Layout GetFinalLayout(NodeId node) const;
    // Rust's `PrintTree::get_debug_label`: the kind of node, for the dump.
    const char* GetDebugLabel(NodeId node) const;

    bool CacheGet(NodeId node, const LayoutInput& input,
                  LayoutOutput* out) const;
    void CacheStore(NodeId node, const LayoutInput& input,
                    const LayoutOutput& output);
    void CacheClear(NodeId node);

    float ResolveCalcValue(const void* handle, float basis) const {
        return calc.Resolve(handle, basis);
    }

    // Dispatches on the node's display style and whether it has children.
    // `blockCtx` is only ever non-null on the block layout path.
    LayoutOutput ComputeChildLayout(NodeId node, LayoutInput inputs);
    LayoutOutput ComputeBlockChildLayout(NodeId node, LayoutInput inputs,
                                         BlockContext* blockCtx);

    // Rust's `LayoutPartialTreeExt` conveniences.
    float MeasureChildSize(NodeId node, SizeFOpt knownDimensions,
                           SizeFOpt parentSize, SizeAvail availableSpace,
                           SizingMode sizingMode, AbsoluteAxis axis,
                           LineBool verticalMarginsAreCollapsible);
    SizeF MeasureChildSizeBoth(NodeId node, SizeFOpt knownDimensions,
                               SizeFOpt parentSize, SizeAvail availableSpace,
                               SizingMode sizingMode,
                               LineBool verticalMarginsAreCollapsible);
    LayoutOutput PerformChildLayout(NodeId node, SizeFOpt knownDimensions,
                                    SizeFOpt parentSize,
                                    SizeAvail availableSpace,
                                    SizingMode sizingMode,
                                    LineBool verticalMarginsAreCollapsible);

    // Null for a stale or never-minted id.
    NodeData* Get(NodeId node) const;
};

} // namespace taffy

#endif // GPUI_TAFFY_TAFFY_TREE_H_
